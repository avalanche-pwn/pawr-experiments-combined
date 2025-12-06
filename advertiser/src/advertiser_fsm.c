#include "advertiser_fsm.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
#define FSM "[FSM] "
#define INFO "[INFO] "
#define ACK "[ACK] "
#define RECEIVED "[RECV] "

#define NUM_RSP_SLOTS 103
#define MAX_NUM_SUBEVENTS 46
#define EVENTS_PER_BLOCK 3
#define PACKET_SIZE 5
#define NAME_LEN 30

typedef struct {
    uint16_t dev_id;
    uint8_t inactive_for;
} slot_data_t;

typedef enum {
    INITIALIZE,
    ADVERTISING,
    FAULT_HANDLING,
    SOFT_REBOOT,
    NUM_STATES
} state_t;
typedef state_t state_func_t();

static void request_cb(struct bt_le_ext_adv *adv,
                       const struct bt_le_per_adv_data_request *request);
static void response_cb(struct bt_le_ext_adv *adv,
                        struct bt_le_per_adv_response_info *info,
                        struct net_buf_simple *buf);

static void button_cb(const struct device *dev, struct gpio_callback *cb);

static state_t init();
static state_t advertising();
static state_t fault_handling();
static state_t soft_reboot();

static const char *state_str(state_t s);
static state_t run_state();

K_SEM_DEFINE(reboot_sem, 0, 1);

static register_data_t reserve_slot();
void init_bufs(void);
static int set_adv_data();

static state_t curr_state = INITIALIZE;
state_func_t *const states[NUM_STATES] = {[INITIALIZE] = &init,
                                          [ADVERTISING] = &advertising,
                                          [FAULT_HANDLING] = &fault_handling,
                                          [SOFT_REBOOT] = &soft_reboot};

K_MUTEX_DEFINE(reserve_mutex);
static register_data_t last_used = {.subevent = 0,
                                    .rsp_slot = CONFIG_NUM_REGISTER_SLOTS};

register_data_t register_subevent_data[CONFIG_NUM_REGISTER_SLOTS];

#define TO_SEND_BUF_SIZE 251

static struct bt_le_per_adv_subevent_data_params
    subevent_data_params[MAX_NUM_SUBEVENTS];
static struct net_buf_simple bufs[MAX_NUM_SUBEVENTS];
static uint8_t backing_store[MAX_NUM_SUBEVENTS][TO_SEND_BUF_SIZE];

BUILD_ASSERT(ARRAY_SIZE(bufs) == ARRAY_SIZE(subevent_data_params));

static slot_data_t rsp_slots[MAX_NUM_SUBEVENTS][NUM_RSP_SLOTS];
static rsp_data_t current_rsp;

static struct bt_le_ext_adv *pawr_adv;
subevent_sel_info_t selection_data;

NET_BUF_SIMPLE_DEFINE_STATIC(adv_data, sizeof(advertisement_data_t) + HASH_LEN);
static uint8_t adv_flags = (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR);

// crypto
static crypto_counter_t counter = {.storage_uid = COUNTER_ID};
static struct bt_le_per_adv_param per_adv_params = {
    .interval_min = 2000,
    .interval_max = 2000,
    .options = 0,
    .num_subevents = MAX_NUM_SUBEVENTS,
    .subevent_interval = 43,
    .response_slot_delay = 0x1,
    .response_slot_spacing = 0x4, // Needs to be at least 2
    .num_response_slots = NUM_RSP_SLOTS,
};

static const struct bt_le_ext_adv_cb adv_cb = {
    .pawr_data_request = request_cb,
    .pawr_response = response_cb,
};

static uint8_t subevent_req_counter = 0;

static void button_cb(const struct device *dev, struct gpio_callback *cb) {
    k_sem_give(&reboot_sem);
}

static uint64_t rollover;
static void request_cb(struct bt_le_ext_adv *adv,
                       const struct bt_le_per_adv_data_request *request) {
    int err;
    uint8_t to_send;
    ack_data_t d;
    subevent_data_t subevent_data;
    ack_data_t ack_data[NUM_RSP_SLOTS] = {0};
    if (rollover > 0) {
        counter.value += rollover;
        rollover = 0;
    }

    subevent_data._register_data_count = 0;
    subevent_data._ack_data_count = NUM_RSP_SLOTS;
    subevent_data.register_data = register_subevent_data;
    subevent_data.ack_data = ack_data;

    to_send = MIN(request->count, ARRAY_SIZE(subevent_data_params));

    for (size_t i = 0; i < to_send; i++) {
        size_t subevent = (request->start + i) % per_adv_params.num_subevents;
        if (subevent == 0) {
            rollover++;
            if (set_adv_data() != 0)
                LOG_ERR("Couldn't update adv data");
        }
        // Ignore register slots while adding to free list

        for (size_t j = 0; j < NUM_RSP_SLOTS; j++) {
            slot_data_t *s = &rsp_slots[subevent][j];
            s->inactive_for++;
            if (s->dev_id != 0 &&
                s->inactive_for > 3 * EVENTS_PER_BLOCK) {
                LOG_INF(INFO "Device with id %d, disconnected", s->dev_id);
                s->dev_id = 0;
                s->inactive_for = 0;

                register_data_t freed = {subevent, j};
                if (free_list_append(freed) != 0) {
                    LOG_WRN(INFO "free list full");
                    // TODO handle this
                }
            }
            if (s->inactive_for == 1 && s->dev_id != 0) {
                // There was data in prev slot, return ack
                d = (ack_data_t){.ack_id = s->dev_id};
                LOG_INF(ACK "1");
            } else {
                // There wasn't any data in prev slot return nack
                d = (ack_data_t){.ack_id = 0};
            }
            ack_data[j] = d;
        }
        subevent_data.counter = counter.value + rollover;

        net_buf_simple_reset(&bufs[i]);
        subevent_data_with_reg_serialize(&subevent_data, &bufs[i]);
        sign_message(&bufs[i], ADVERTISER_KEY_ID);

        subevent_data_params[i].subevent =
            (request->start + i) % per_adv_params.num_subevents;
        subevent_data_params[i].response_slot_start = 0;
        subevent_data_params[i].response_slot_count = NUM_RSP_SLOTS;
        subevent_data_params[i].data = &bufs[i];
        subevent_req_counter += 1;
        err = bt_le_per_adv_set_subevent_data(adv, 1, &subevent_data_params[i]);
        if (err) {
            LOG_WRN(INFO "Failed to set subevent data (err %d)", err);
        }
    }
}

static void response_cb(struct bt_le_ext_adv *adv,
                        struct bt_le_per_adv_response_info *info,
                        struct net_buf_simple *buf) {
    transfer_error_t transfer_err;
    response_data_t response;
    struct net_buf_simple_state parse_state;
    if (buf) {
        LOG_INF(INFO "Response: subevent %d, slot %d", info->subevent,
                info->response_slot);

        slot_data_t *slot = &rsp_slots[info->subevent][info->response_slot];

        net_buf_simple_save(buf, &parse_state);
        size_t to_save = HASH_LEN + sizeof(counter.value);
        if (buf->len < to_save) {
            LOG_WRN("message to short");
            return;
        }
        net_buf_simple_remove_mem(buf, to_save);

        transfer_err = response_data_deserialize(&response, buf);
        if (transfer_err) {
            LOG_WRN("Couldn't deserialize data");
            return;
        }
        current_rsp = response.rsp_metadata;

        net_buf_simple_restore(buf, &parse_state);
        transfer_err =
            verify_message(buf, MIN_SCANNER_KEY_ID + current_rsp.sender_id - 1,
                           &counter.value);
        if (transfer_err) {
            LOG_WRN("FAILED to verify device, id: %d, err: %d",
                    current_rsp.sender_id, transfer_err);
            slot->dev_id = 0;
            register_data_t rd = (register_data_t){
                .subevent = info->subevent, .rsp_slot = info->response_slot};
            free_list_append(rd);
            return;
        }
        if (set_adv_data() != 0) {
            LOG_ERR("FAILED TO update adv data");
        }

        if (slot->dev_id == 0) {
            // slot is empty -> register new device
            LOG_INF(INFO "New device registerd id: %d, sub: %d, slot: %d",
                    current_rsp.sender_id, info->subevent, info->response_slot);
            slot->dev_id = current_rsp.sender_id;
            slot->inactive_for = 0;

            for (size_t i = 0; i < CONFIG_NUM_REGISTER_SLOTS; i++) {
                if (info->subevent == register_subevent_data[i].subevent &&
                    info->response_slot == register_subevent_data[i].rsp_slot) {
                    register_subevent_data[i] = reserve_slot();
                    break;
                }
            }
            if (set_adv_data() != 0) {
                LOG_ERR("FAILED TO update adv data");
            }

            return;
        } else if (slot->dev_id == current_rsp.sender_id) {
            // Got response from excepted sender
            slot->inactive_for = 0;
            LOG_INF(RECEIVED "%d, 1, %d, %d", slot->dev_id, info->rssi, current_rsp.counter);
            return;
        }
    }
}

static int set_adv_data() {
    advertisement_data_t to_advertise = {.reg_data = register_subevent_data,
                                         .selection_info = selection_data,
                                         .counter = counter.value};
    net_buf_simple_reset(&adv_data);
    advertisement_data_serialize(&to_advertise, &adv_data);
    sign_message(&adv_data, ADVERTISER_KEY_ID);

    const struct bt_data ad[] = {
        BT_DATA(BT_DATA_FLAGS, &adv_flags, sizeof(adv_flags)),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data.data, adv_data.len),
    };
    return bt_le_ext_adv_set_data(pawr_adv, ad, ARRAY_SIZE(ad), NULL, 0);
}

// FSM definitions

static state_t init() {
    int err;
    psa_status_t psa_err;
    k_sleep(K_SECONDS(5));

#ifdef CONFIG_INTERACTIVE
    init_led(master_led);
    init_button(&button_cb);
#endif // CONFIG_INTERACTIVE
    init_bufs();
    LOG_INF("Device id: 0");

    psa_err = crypto_init();
    if (psa_err != PSA_SUCCESS)
        return FAULT_HANDLING;

    psa_err = crypto_secure_counter_init(&counter);
    if (psa_err != PSA_SUCCESS)
        return FAULT_HANDLING;

    LOG_INF(INFO "Starting advertiser, courrent counter %lld", counter.value);

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR(INFO "Bluetooth init failed (err %d)", err);
        return FAULT_HANDLING;
    }

    err = bt_le_ext_adv_create(BT_LE_EXT_ADV_NCONN, &adv_cb, &pawr_adv);
    if (err) {
        LOG_ERR(INFO "Failed to create advertising set (err %d)", err);
        return FAULT_HANDLING;
    }

    /* Set periodic advertising parameters */
    per_adv_params.num_subevents = MAX_NUM_SUBEVENTS;
    err = bt_le_per_adv_set_param(pawr_adv, &per_adv_params);
    if (err) {
        LOG_ERR(INFO "Failed to set periodic advertising parameters (err %d)",
                err);
        return FAULT_HANDLING;
    }

    selection_data.num_reg_slots = CONFIG_NUM_REGISTER_SLOTS;

    err = set_adv_data();
    if (err) {
        LOG_ERR(INFO "Failed to set Extended ADV data (err %d)", err);
        return FAULT_HANDLING;
    }

    err = bt_le_per_adv_start(pawr_adv);
    if (err) {
        LOG_ERR("Failed to enable periodic advertising (err %d)", err);
        return FAULT_HANDLING;
    }
    LOG_INF("Started periodic adv");

    err = bt_le_ext_adv_start(pawr_adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        LOG_ERR("Failed to start extended advertising (err %d)", err);
        return FAULT_HANDLING;
    }
    LOG_INF("Started extended adv");

    return ADVERTISING;
}

static state_t advertising() {
    while (k_sem_take(&reboot_sem, K_SECONDS(10)) != 0) {
        LOG_INF(INFO "Still alive");
    }
    return SOFT_REBOOT;
}

static state_t fault_handling() {
    crypto_secure_counter_commit(&counter);
    LOG_ERR(INFO "Received a fault rebooting");
    sys_reboot(SYS_REBOOT_COLD);
}

static state_t soft_reboot() {
    LOG_INF("Reboot requested, saving counter and rebooting");
    crypto_secure_counter_commit(&counter);
    sys_reboot(SYS_REBOOT_COLD);
}

static const char *state_str(state_t s) {
    switch (s) {
    case INITIALIZE:
        return "INITIALIZE";
    case ADVERTISING:
        return "ADVERTISING";
    case SOFT_REBOOT:
        return "SOFT_REBOOT";
    default:
        return "INVALID_STATE";
    }
}

static register_data_t reserve_slot() {
    register_data_t ret;

    if (free_list_pop(&ret) == 0)
        return ret;

    k_mutex_lock(&reserve_mutex, K_FOREVER);

    memcpy(&ret, &last_used, sizeof(ret));

    if (last_used.rsp_slot + 1 == NUM_RSP_SLOTS) {
        last_used.subevent++;
        last_used.rsp_slot = 0;
    } else {
        last_used.rsp_slot++;
    }

    k_mutex_unlock(&reserve_mutex);

    return ret;
}

void init_bufs(void) {
    for (size_t i = 0; i < CONFIG_NUM_REGISTER_SLOTS; i++) {
        register_data_t sel_slot = reserve_slot();
        register_subevent_data[i] = sel_slot;
    }
    for (size_t i = 0; i < MAX_NUM_SUBEVENTS; i++) {
        net_buf_simple_init_with_data(&bufs[i], &backing_store[i],
                                      TO_SEND_BUF_SIZE);
    }
}

static state_t run_state() { return states[curr_state](); }

void loop() {
    for (;;) {
        LOG_INF(FSM "Transitioning to state %s", state_str(curr_state));
        curr_state = run_state();
    }
}
