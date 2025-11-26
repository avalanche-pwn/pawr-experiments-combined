#include "scanner_fsm.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
#define FSM "[FSM] "
#define INFO "[INFO] "

#define SCALE_INTERVAL_TO_TIMEOUT(interval) (interval * 5 / 40)
#define NUM_RSP_SLOTS 10

/**
 * Enum for states of this fsm.
 */
typedef enum {
    INITIALIZE,
    FAULT_HANDLING,
    SYNCING,
    REGISTERING,
    CONFIRMING,
    SLEEPING,
    ENABLED,
    NUM_STATES
} state_t;

/**
 * Enum for faults/events that can happen during fsm cycle.
 */
typedef enum {
    EVT_NO_FAULT,
    EVT_BLE_ENABLE_FAILED,
    EVT_BLE_SCAN_START_FAILED,
    EVT_BLE_SYNC_TIMEOUT,
    EVT_BLE_SYNC_DELETED,
    EVT_CONFIRMATION_FAILED,
    EVT_DIDNT_RECEIVE_ACK,
    EVT_DATA_GENERATED,
    EVT_GOT_ACK,
    EVT_INVALID_HASH
} evt_t;

typedef state_t state_func();

/**
 * Struct for data returned from advertisement.
 * TODO should be replaced by bt_data.
 */
typedef struct __attribute__((__packed__)) {
    uint8_t len;
    uint8_t flags;
} excepted_data_t;

static void sync_cb(struct bt_le_per_adv_sync *sync,
                    struct bt_le_per_adv_sync_synced_info *info);

/**
 * \brief Callback for sync termination.
 * This callback stays the same for the entire time. When scan is terminated it
 * set's the fault reason which is for current state to handle properly.
 */
static void term_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_term_info *info);

/**
 * \brief Callback for receiving data from advertiser during registration.
 * It replies in one of the slots available by sel_info. The slot is
 * choosen randomly. This is to hopefully not colide with other devices trying
 * to register at the same time.
 */
static void register_recv_cb(struct bt_le_per_adv_sync *sync,
                             const struct bt_le_per_adv_sync_recv_info *info,
                             struct net_buf_simple *buf);
/**
 * \brief Callback for receiving data from advertiser during confirmation state.
 * It excepts to receive proper ack, if it doesn't it should throw the scanner
 * back into registering mode. This could happen either because of interference
 * or other device taking the spot.
 */
static void confirm_recv_cb(struct bt_le_per_adv_sync *sync,
                            const struct bt_le_per_adv_sync_recv_info *info,
                            struct net_buf_simple *buf);

/**
 * Callback used for sending ack to advertiser
 */
static void ack_recv_cb(struct bt_le_per_adv_sync *sync,
                        const struct bt_le_per_adv_sync_recv_info *info,
                        struct net_buf_simple *buf);

/**
 * \brief Callback used for initialising pawr.
 * When ext adv packet is handled it sets proper sync parameters.
 */
static void scan_recv_cb(const struct bt_le_scan_recv_info *info,
                         struct net_buf_simple *buf);

/**
 * \brief Sets the response data via bt_le_per_adv_set_response_data
 * Since we are always using the same data there is no need to repeat this
 * process each time.
 *
 * \return 0 on success otherwies error returned by
 * bt_le_per_adv_set_response_data.
 */
static int set_rsp_data(struct bt_le_per_adv_sync *sync,
                        const struct bt_le_per_adv_sync_recv_info *info, response_data_t *resp);

/**
 * Initialise response buffer with data
 */
static void data_generated_cb();

static state_t init();
static state_t syncing();
static state_t handle_fault();
static state_t registering();
static state_t confirming();
static state_t sleeping();
static state_t enabled();

/**
 * Runs current state as defined by current_state
 * \return Next state_t that should be processed
 */
static state_t run_state();
static char *state_str(state_t s);

static state_func *const states[NUM_STATES] = {
    [INITIALIZE] = &init,       [FAULT_HANDLING] = &handle_fault,
    [SYNCING] = &syncing,       [REGISTERING] = &registering,
    [CONFIRMING] = &confirming, [SLEEPING] = &sleeping,
    [ENABLED] = &enabled};

/**
 * Current state of fsm.
 */
static state_t curr_state = INITIALIZE;
/**
 * Reason why state exited
 */
static atomic_t fault_reason = ATOMIC_INIT(EVT_NO_FAULT);

/**
 * Information about how to select subevent
 */
static subevent_sel_info_t sel_info;
static register_data_t selected_slot;

/**
 * Current ble sync object.
 */
static struct bt_le_per_adv_sync *default_sync;

/**
 * Netbuf for responses from scanner
 */
NET_BUF_SIMPLE_DEFINE_STATIC(scanner_rsp_buf, 251);
NET_BUF_SIMPLE_DEFINE_STATIC(random, UNUSED_DATA_LEN);
static response_data_t response;

/**
 * Parameters for scanning for ext adv packets.
 */
static const struct bt_le_scan_param scan_param = {
    .type = BT_HCI_LE_SCAN_ACTIVE,
    .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
    .interval = 0x00A0, // 100 ms
    .window = 0x0050,   // 50 ms
};

K_SEM_DEFINE(synced_evt_sem, 0, 1);
K_SEM_DEFINE(register_evt_sem, 0, 1);
K_SEM_DEFINE(synced_sem, 0, 1);

/**
 * Data which we send to advertiser.
 */
static const rsp_data_t rsp_data_i = {.sender_id = CONFIG_SCANNER_ID};

/**
 * \brief Number of consecutive uncofirmed responses in confirming state.
 * If this number reaches CONFIG_MAX_UNCONFIRMED_TICKS, the device will try to
 * register again.
 */
static uint8_t unconfirmed_ticks = 0;

/**
 * \brief Callbacks for periodic advertisment sync.
 * This can be changed in runtime depending on state.
 * Initial values are for syncing register state.
 */
static struct bt_le_per_adv_sync_cb sync_callbacks = {
    .synced = sync_cb,
    .term = term_cb,
    .recv = NULL,
};

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv_cb,
    .timeout = NULL,
};

static data_generator_config_t generator_config = {
    .data = &random,
    .interval = CONFIG_BLOCK_TIME,
    .init_buf = NULL,
    .generated = &data_generated_cb,
};

static crypto_counter_t counter = {.storage_uid = COUNTER_ID};

#ifdef CONFIG_INTERACTIVE
// When building for boards we use the led defined here
// to indicate that this board is a sensor
#define SLAVE_LED_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(SLAVE_LED_NODE, gpios);
#endif

static void sync_cb(struct bt_le_per_adv_sync *sync,
                    struct bt_le_per_adv_sync_synced_info *info) {
    struct bt_le_per_adv_sync_subevent_params params;
    uint8_t subevents[1];
    char le_addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
    LOG_INF(INFO "Synced to %s with %d subevents", le_addr,
            info->num_subevents);

    default_sync = sync;

    params.properties = 0;
    params.num_subevents = 1;
    params.subevents = subevents;
    subevents[0] = selected_slot.subevent;

    err = bt_le_per_adv_sync_subevent(sync, &params);
    if (err) {
        LOG_WRN(INFO "Failed to set subevents to sync to (err %d)", err);
    } else {
        LOG_INF(INFO "Changed sync to subevent %d", subevents[0]);
    }
}

static void term_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_term_info *info) {
    char le_addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

    LOG_WRN(INFO "Sync terminated (reason %d)", info->reason);

    default_sync = NULL;

    atomic_set(&fault_reason, EVT_BLE_SYNC_TIMEOUT);
    if (info->reason == 22)
        atomic_set(&fault_reason, EVT_BLE_SYNC_DELETED);

    k_sem_give(&synced_evt_sem);
}

static void register_recv_cb(struct bt_le_per_adv_sync *sync,
                             const struct bt_le_per_adv_sync_recv_info *info,
                             struct net_buf_simple *buf) {
    subevent_data_t subevent_data;
    register_data_t reg_data[sel_info.num_reg_slots];
    ack_data_t ack_data[NUM_RSP_SLOTS];

    int err;

    subevent_data._register_data_count = sel_info.num_reg_slots;
    subevent_data._ack_data_count = NUM_RSP_SLOTS;
    subevent_data.register_data = reg_data;
    subevent_data.ack_data = ack_data;

    sync_callbacks.recv = NULL;

    if (buf && buf->len) {
        selected_slot.rsp_slot = sys_rand8_get() % sel_info.num_reg_slots;

        err = verify_message(buf, ADVERTISER_KEY_ID, &counter.value);
        if (err != 0) {
            LOG_WRN(INFO "Failed to verify message");
            atomic_set(&fault_reason, EVT_INVALID_HASH);
            k_sem_give(&register_evt_sem);
            return;
        }

        err = subevent_data_with_reg_deserialize(&subevent_data, buf);
        if (err) {
            LOG_WRN(INFO "Failed to deserialize message");
            return;
        }

        LOG_INF("%d", selected_slot.rsp_slot);
        memcpy(&selected_slot,
               &subevent_data.register_data[selected_slot.rsp_slot],
               sizeof(selected_slot));

        k_sem_give(&register_evt_sem);
    } else if (buf) {
        LOG_WRN(INFO "Received empty indication: subevent %d", info->subevent);
    } else {
        LOG_WRN(INFO "Failed to receive indication: subevent %d",
                info->subevent);
    }
}

static void confirm_recv_cb(struct bt_le_per_adv_sync *sync,
                            const struct bt_le_per_adv_sync_recv_info *info,
                            struct net_buf_simple *buf) {
    int err;
    subevent_data_t subevent_data;

    register_data_t reg_data[sel_info.num_reg_slots];
    ack_data_t ack_data[NUM_RSP_SLOTS];

    subevent_data._register_data_count = 0;
    subevent_data._ack_data_count = NUM_RSP_SLOTS;
    subevent_data.register_data = reg_data;
    subevent_data.ack_data = ack_data;

    response_data_t resp;
    resp.rsp_metadata = rsp_data_i;
    resp.data_len = 0;

    if (buf && buf->len) {
        if (selected_slot.subevent == 0) {
            subevent_data._register_data_count = sel_info.num_reg_slots;
        }

        err = verify_message(buf, ADVERTISER_KEY_ID, &counter.value);
        if (err != 0) {
            LOG_WRN(INFO "Failed to verify message");
            atomic_set(&fault_reason, EVT_INVALID_HASH);
            k_sem_give(&synced_evt_sem);
            return;
        }

        resp.counter = counter.value;

        LOG_INF("Test %lld", counter.value);
        err = subevent_data_with_reg_deserialize(&subevent_data, buf);
        if (err) {
            LOG_WRN(INFO "Failed to deserialize message");
        }

        if (err != 0 || subevent_data.ack_data[selected_slot.rsp_slot].ack_id !=
            CONFIG_SCANNER_ID) {
            err = set_rsp_data(sync, info, &resp);
            if (err) {
                LOG_WRN(INFO "Failed to send response (err %d)", err);
            }
            if (unconfirmed_ticks != 0)
                LOG_WRN("Failed to confirm reservation");
            unconfirmed_ticks += 1;

            if (unconfirmed_ticks >= CONFIG_MAX_UNCONFIRMED_TICKS) {
                atomic_set(&fault_reason, EVT_CONFIRMATION_FAILED);
                k_sem_give(&synced_evt_sem);
                return;
            }
            return;
        }

        atomic_set(&fault_reason, EVT_NO_FAULT);
        k_sem_give(&synced_evt_sem);

    } else if (buf) {
        LOG_WRN(INFO "Received empty indication: subevent %d", info->subevent);
    } else {
        LOG_WRN(INFO "Failed to receive indication: subevent %d",
                info->subevent);
    }
}

static void scan_recv_cb(const struct bt_le_scan_recv_info *info,
                         struct net_buf_simple *buf) {
    char addr_str[BT_ADDR_LE_STR_LEN];
    struct bt_le_per_adv_sync_param sync_create_param;
    struct bt_le_per_adv_sync *sync;
    int err;

    if (!info || !info->addr) {
        return;
    }

    // Only process Extended ADV with Periodic ADV info (SyncInfo)
    if (!(info->adv_props & BT_GAP_ADV_PROP_EXT_ADV)) {
        return; // Not Extended ADV
    }

    if (info->interval == 0) {
        // Extended ADV without SyncInfo - keep scanning
        return;
    }
    uint8_t read = 0;
    excepted_data_t data_desc;
    while (buf->len - read > sizeof(data_desc)) {
        memcpy(&data_desc, buf->data + read, sizeof(data_desc));
        if (data_desc.len > 0 && data_desc.flags == BT_DATA_MANUFACTURER_DATA) {
            memcpy(&sel_info, buf->data + read + sizeof(data_desc),
                   data_desc.len - 1);
        }
        read += data_desc.len + 1;
    }

    bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));

    bt_addr_le_copy(&sync_create_param.addr, info->addr);
    sync_create_param.options = 0;
    sync_create_param.sid = info->sid;
    sync_create_param.skip = 0;
    sync_create_param.timeout =
        SCALE_INTERVAL_TO_TIMEOUT(info->interval) * CONFIG_NUM_FAILED_SYNC;
    LOG_INF(INFO "Establisehd sync interval %d", info->interval);

    err = bt_le_per_adv_sync_create(&sync_create_param, &sync);

    default_sync = sync;

    if (err) {
        LOG_WRN(INFO "Failed to create sync to %s (err %d)", addr_str, err);
        return;
    }

    LOG_INF(INFO "Creating sync to %s (SID=%u)...", addr_str, info->sid);
    err = bt_le_scan_stop();
    if (err) {
        LOG_ERR(INFO "Couldn't stop le scanning");
        return;
    }
    LOG_INF(INFO "Stopped le scanning");
    k_sem_give(&synced_sem);
}

static void ack_recv_cb(struct bt_le_per_adv_sync *sync,
                        const struct bt_le_per_adv_sync_recv_info *info,
                        struct net_buf_simple *buf) {
    int err;
    LOG_INF("Current counter %lld", counter.value);

    register_data_t reg_data[sel_info.num_reg_slots];
    ack_data_t ack_data[NUM_RSP_SLOTS];

    subevent_data_t subevent_data;

    subevent_data._register_data_count = 0;
    subevent_data._ack_data_count = NUM_RSP_SLOTS;
    subevent_data.register_data = reg_data;
    subevent_data.ack_data = ack_data;

    if (buf && buf->len) {
        if (selected_slot.subevent == 0) {
            subevent_data._register_data_count = sel_info.num_reg_slots;
        }

        err = verify_message(buf, ADVERTISER_KEY_ID, &counter.value);
        if (err != 0) {
            LOG_WRN("Failed to verify hash");
            atomic_set(&fault_reason, EVT_INVALID_HASH);
            k_sem_give(&synced_evt_sem);
            return;
        }

        err = subevent_data_with_reg_deserialize(&subevent_data, buf);

        if (err != 0 || subevent_data.ack_data[selected_slot.rsp_slot].ack_id !=
                            CONFIG_SCANNER_ID) {
            if (unconfirmed_ticks != 0)
                LOG_WRN("Didn't receive ack");
            unconfirmed_ticks += 1;
        } else {
            unconfirmed_ticks = 0;
            sync_callbacks.recv = NULL;
            atomic_set(&fault_reason, EVT_GOT_ACK);
            k_sem_give(&synced_evt_sem);
            return;
        }

        if (unconfirmed_ticks >= CONFIG_MAX_UNCONFIRMED_TICKS) {
            atomic_set(&fault_reason, EVT_DIDNT_RECEIVE_ACK);
            k_sem_give(&synced_evt_sem);
            return;
        }

        response.counter = counter.value;
        err = set_rsp_data(sync, info, &response);
        if (err) {
            LOG_WRN(INFO "Failed to send response (err %d)", err);
        }
    } else if (buf) {
        LOG_WRN(INFO "Received empty indication: subevent %d", info->subevent);
    } else {
        LOG_WRN(INFO "Failed to receive indication: subevent %d",
                info->subevent);
    }
}

static int set_rsp_data(struct bt_le_per_adv_sync *sync,
                        const struct bt_le_per_adv_sync_recv_info *info, response_data_t *resp) {
    static struct bt_le_per_adv_response_params rsp_params;

    rsp_params.request_event = info->periodic_event_counter;
    rsp_params.request_subevent = info->subevent;
    /* Respond in current subevent and assigned response slot */
    rsp_params.response_subevent = info->subevent;
    rsp_params.response_slot = selected_slot.rsp_slot;

    net_buf_simple_reset(&scanner_rsp_buf);
    response_data_serialize(resp, &scanner_rsp_buf);
    sign_message(&scanner_rsp_buf, MIN_SCANNER_KEY_ID);

    LOG_INF(INFO "Indication: subevent %d, responding in slot %d, len: %d",
            info->subevent, selected_slot.rsp_slot, scanner_rsp_buf.len);

    int ret =  bt_le_per_adv_set_response_data(sync, &rsp_params, &scanner_rsp_buf);
    counter.value++;
    return ret;
}

static state_t init() {
    int err;
    psa_status_t psa_err;
#ifdef CONFIG_INTERACTIVE
    init_led(led);
#endif

    if (crypto_init() != PSA_SUCCESS) {
        LOG_WRN("FAILED TO INIT PSA");
        return FAULT_HANDLING;
    }

    if ((psa_err = crypto_secure_counter_init(&counter)) != PSA_SUCCESS) {
        LOG_WRN("FAILED TO INIT SECURE COUNTER (err: %d)", psa_err);
        return FAULT_HANDLING;
    }

    LOG_INF(INFO "Device with id %d initialised with counter %lld",
            CONFIG_SCANNER_ID, counter.value);

    selected_slot.subevent = 0;

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR(INFO "Bluetooth init failed (err %d)", err);
        atomic_set(&fault_reason, EVT_BLE_ENABLE_FAILED);
        return FAULT_HANDLING;
    }

    bt_le_scan_cb_register(&scan_callbacks);
    return SYNCING;
}

static state_t syncing() {
    int err;
    // sync_callbacks.recv = &register_recv_cb;

    bt_le_per_adv_sync_cb_register(&sync_callbacks);
    err = bt_le_scan_start(&scan_param, NULL);

    if (err) {
        LOG_ERR(INFO "Failed to start scanning for sync %d", err);
        atomic_set(&fault_reason, EVT_BLE_SCAN_START_FAILED);
        return FAULT_HANDLING;
    }

    for (size_t sync_iters = 0;; sync_iters++) {
        if (k_sem_take(&synced_sem, K_SECONDS(10))) {
            LOG_INF(INFO "Still syncing, iterations %d", sync_iters);
            continue;
        }
        break;
    }
    return REGISTERING;
}

static state_t handle_fault() {
    LOG_ERR(INFO "Handling fault %ld", atomic_get(&fault_reason));
    // Wait for a while so that buffer get's flushed
    k_sleep(K_SECONDS(10));
    sys_reboot(SYS_REBOOT_COLD);
}

static state_t registering() {
    struct bt_le_per_adv_sync_param sync_create_param;
    struct bt_le_per_adv_sync_info info;
    struct bt_le_per_adv_sync *sync;
    int err;

    sync_callbacks.recv = &register_recv_cb;
    k_sem_take(&register_evt_sem, K_FOREVER);
    k_sem_reset(&register_evt_sem);
    if (atomic_get(&fault_reason) == EVT_INVALID_HASH) {
        sync_callbacks.recv = NULL;
        bt_le_per_adv_sync_delete(default_sync);
        return SYNCING;
    }

    bt_le_per_adv_sync_get_info(default_sync, &info);

    bt_addr_le_copy(&sync_create_param.addr, &info.addr);
    sync_create_param.options = 0;
    sync_create_param.sid = info.sid;
    sync_create_param.skip = 0;
    sync_create_param.timeout =
        SCALE_INTERVAL_TO_TIMEOUT(info.interval) * CONFIG_NUM_FAILED_SYNC;

    LOG_INF(INFO "Establisehd sync interval %d", info.interval);

    bt_le_per_adv_sync_delete(default_sync);
    err = bt_le_per_adv_sync_create(&sync_create_param, &sync);

    default_sync = sync;

    if (err) {
        LOG_WRN(INFO "Failed to recreate sync (err: %d)", err);
        // Wait for a while so that buffer get's flushed
        return FAULT_HANDLING;
    }

    k_sem_take(&synced_evt_sem, K_FOREVER);
    evt_t curr = atomic_get(&fault_reason);
    if (curr != EVT_BLE_SYNC_DELETED)
        return FAULT_HANDLING;
    return CONFIRMING;
}

static state_t confirming() {
    unconfirmed_ticks = 0;
    k_sleep(K_SECONDS(1));
    sync_callbacks.recv = &confirm_recv_cb;

    k_sem_take(&synced_evt_sem, K_FOREVER);
    k_sem_reset(&synced_evt_sem);
    switch (atomic_get(&fault_reason)) {
        case EVT_CONFIRMATION_FAILED:
            return REGISTERING;
        case EVT_INVALID_HASH:
            return SYNCING;
        case EVT_BLE_SYNC_DELETED:
        case EVT_BLE_SYNC_TIMEOUT:
            return SYNCING;
    }
    data_generator_init(&generator_config);
    return SLEEPING;
}

static state_t sleeping() {
    bt_le_per_adv_sync_recv_disable(default_sync);
    state_t ret = FAULT_HANDLING;

    for (;;) {
        if (k_sem_take(&synced_evt_sem, K_SECONDS(30))) {
            LOG_INF(INFO "Still alive");
            continue;
        }
        evt_t curr = atomic_get(&fault_reason);

        k_sem_init(&synced_evt_sem, 0, 1);
        switch (curr) {
        case EVT_NO_FAULT:
            continue;
        case EVT_INVALID_HASH:
        case EVT_BLE_SYNC_TIMEOUT:
            ret = SYNCING;
            goto ret_generator_stop;
        case EVT_DATA_GENERATED:
            ret = ENABLED;
            goto ret_default;
        default:
            goto ret_generator_stop;
        }
    }
ret_generator_stop:
    data_generator_stop();
ret_default:
    sync_callbacks.recv = NULL;
    return ret;
}

static state_t enabled() {
    sync_callbacks.recv = &ack_recv_cb;
    unconfirmed_ticks = 0;
    bt_le_per_adv_sync_recv_enable(default_sync);
    k_sem_take(&synced_evt_sem, K_FOREVER);
    k_sem_reset(&synced_evt_sem);
    switch (atomic_get(&fault_reason)) {
    case EVT_GOT_ACK:
        LOG_INF(INFO "Got ACK");
        bt_le_per_adv_sync_recv_disable(default_sync);
        return SLEEPING;
    case EVT_DIDNT_RECEIVE_ACK:
        LOG_INF(INFO "Failed to receive ACK in %d events, reregistering",
                unconfirmed_ticks);
        return REGISTERING;
    case EVT_INVALID_HASH:
    case EVT_BLE_SYNC_TIMEOUT:
        return SYNCING;
    default:
        return FAULT_HANDLING;
    }
}

static void data_generated_cb() {
    response.rsp_metadata = rsp_data_i;
    response.data = random.data;
    response.data_len = UNUSED_DATA_LEN;

    atomic_set(&fault_reason, EVT_DATA_GENERATED);
    k_sem_give(&synced_evt_sem);
    return;
}

static state_t run_state() { return states[curr_state](); }

static char *state_str(state_t s) {
    switch (s) {
    case INITIALIZE:
        return "INITIALIZE";
    case FAULT_HANDLING:
        return "FAULT_HANDLING";
    case SYNCING:
        return "SYNCING";
    case SLEEPING:
        return "SLEEPING";
    case ENABLED:
        return "ENABLED";
    case REGISTERING:
        return "REGISTERING";
    case CONFIRMING:
        return "CONFIRMING";
    default:
        return "INVALID_STATE";
    }
}

void loop() {
    for (;;) {
        LOG_INF(FSM "Transitioning to state %s", state_str(curr_state));
        curr_state = run_state();
    }
}
