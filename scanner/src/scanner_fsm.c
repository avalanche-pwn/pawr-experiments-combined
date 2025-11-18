#include "scanner_fsm.h"
#include "zephyr/kernel.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/sys/atomic.h>

#include <zephyr/logging/log.h>

#include <zephyr/random/random.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <app/lib/common.h>
#include <app/lib/transfer.h>
#ifdef CONFIG_INTERACTIVE
#include <app/lib/interactive.h>
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
#define FSM "[FSM] "
#define INFO "[INFO] "

#define SCALE_INTERVAL_TO_TIMEOUT(interval) (interval * 5 / 40)

typedef enum {
    INITIALIZE,
    FAULT_HANDLING,
    SYNCING,
    REGISTERING,
    CONFIRMING,
    SYNCED,
    NUM_STATES
} state;

typedef enum {
    NO_FAULT,
    BLE_ENABLE_FAILED,
    BLE_SCAN_START_FAILED,
    BLE_SYNC_TIMEOUT,
    BLE_SYNC_DELETED
} fault;

typedef state state_func();

static state curr_state = INITIALIZE;

static atomic_t fault_reason = ATOMIC_INIT(NO_FAULT);

static struct bt_le_per_adv_response_params rsp_params;
static subevent_sel_info sel_info;
static register_data selected_slot;
static struct bt_le_per_adv_sync *default_sync;
NET_BUF_SIMPLE_DEFINE_STATIC(rsp_buf, CONFIG_BT_PER_ADV_SYNC_BUF_SIZE);

static struct bt_le_scan_param scan_param = {
    .type = BT_HCI_LE_SCAN_ACTIVE,
    .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
    .interval = 0x00A0, // 100 ms
    .window = 0x0050,   // 50 ms
};

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

K_SEM_DEFINE(synced_evt_sem, 0, 1);

static void term_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_term_info *info) {
    char le_addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

    LOG_WRN(INFO "Sync terminated (reason %d)", info->reason);

    default_sync = NULL;

    atomic_set(&fault_reason, BLE_SYNC_TIMEOUT);
    if (info->reason == 22)
        atomic_set(&fault_reason, BLE_SYNC_DELETED);

    k_sem_give(&synced_evt_sem);
}

static bool parse_register_data(struct bt_data *data, void *idx) {
    bool value = true;
    if (*(uint8_t *)idx != selected_slot.rsp_slot)
        goto ret;
    if (data->type != REGISTER_DATA)
        goto ret;

    value = false;
    memcpy(&selected_slot, data->data, sizeof(selected_slot));
    LOG_INF("%d %d", selected_slot.subevent, selected_slot.rsp_slot);

ret:
    *(uint8_t *)idx = *(uint8_t *)idx + 1;
    return value;
}

static const rsp_data rsp_data_i = {.sender_id = CONFIG_SCANNER_ID};

K_SEM_DEFINE(register_evt_sem, 0, 1);
static void register_recv_cb(struct bt_le_per_adv_sync *sync,
                             const struct bt_le_per_adv_sync_recv_info *info,
                             struct net_buf_simple *buf) {
    static struct bt_le_per_adv_response_params rsp_params;
    register_data *recv_data;
    int err;

    if (buf && buf->len) {
        /* Echo the data back to the advertiser */
        net_buf_simple_reset(&rsp_buf);
        net_buf_simple_add_mem(&rsp_buf, &rsp_data_i, sizeof(rsp_data));

        selected_slot.rsp_slot = sys_rand8_get() % sel_info.num_reg_slots;
        rsp_params.request_event = info->periodic_event_counter;
        rsp_params.request_subevent = info->subevent;
        /* Respond in current subevent and assigned response slot */
        rsp_params.response_subevent = info->subevent;
        rsp_params.response_slot = selected_slot.rsp_slot;

        LOG_INF(INFO "Indication: subevent %d, responding in slot %d",
                info->subevent, selected_slot.rsp_slot);
        int idx = 0;

        err = bt_le_per_adv_set_response_data(sync, &rsp_params, &rsp_buf);

        // TODO this probably can cause read over the net bufs len
        // If a malicious device sends shorter buf the pull length might be over
        // it's size
        recv_data = net_buf_simple_pull_mem(buf, sizeof(register_data) *
                                                     sel_info.num_reg_slots);
        memcpy(&selected_slot, &recv_data[selected_slot.rsp_slot],
               sizeof(selected_slot));

        if (err) {
            LOG_WRN(INFO "Failed to send response (err %d)", err);
        }
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
    ack_data *recv_ack_data;
    size_t reg_data_size = 0;
    size_t reg_data_count = 0;

    if (buf && buf->len) {
        /* Echo the data back to the advertiser */
        net_buf_simple_reset(&rsp_buf);
        net_buf_simple_add_mem(&rsp_buf, &rsp_data_i, sizeof(rsp_data));

        rsp_params.request_event = info->periodic_event_counter;
        rsp_params.request_subevent = info->subevent;
        /* Respond in current subevent and assigned response slot */
        rsp_params.response_subevent = info->subevent;
        rsp_params.response_slot = selected_slot.rsp_slot;

        LOG_INF(INFO "Indication: subevent %d, responding in slot %d",
                info->subevent, selected_slot.rsp_slot);

        if (selected_slot.subevent == 0) {
            reg_data_count = sel_info.num_reg_slots;
            reg_data_size = sizeof(register_data) * sel_info.num_reg_slots;
            net_buf_simple_pull_mem(buf, reg_data_size);
        }

        recv_ack_data = net_buf_simple_pull_mem(buf, buf->len);

        LOG_INF("IDX %d", selected_slot.rsp_slot - reg_data_count);

        if (recv_ack_data[selected_slot.rsp_slot - reg_data_count].ack_id !=
            CONFIG_SCANNER_ID) {
            LOG_WRN("Failed to confirm reservation");
        }

        err = bt_le_per_adv_set_response_data(sync, &rsp_params, &rsp_buf);
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
static struct bt_le_per_adv_sync_cb sync_callbacks = {
    .synced = sync_cb,
    .term = term_cb,
    .recv = register_recv_cb,
};

K_SEM_DEFINE(synced_sem, 0, 1);

typedef struct __attribute__((__packed__)) {
    uint8_t len;
    uint8_t flags;
} excepted_data;

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
    excepted_data data_desc;
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
    sync_create_param.skip = 1;
    sync_create_param.timeout =
        SCALE_INTERVAL_TO_TIMEOUT(info->interval) * CONFIG_NUM_FAILED_SYNC;
    LOG_INF(INFO "Establisehd sync interval %d", info->interval);

    err = bt_le_per_adv_sync_create(&sync_create_param, &sync);

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

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv_cb,
    .timeout = NULL,
};

static state init() {
    int err;
#ifdef CONFIG_INTERACTIVE
    init_led(led);
#endif

    selected_slot.subevent = 0;

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR(INFO "Bluetooth init failed (err %d)", err);
        atomic_set(&fault_reason, BLE_ENABLE_FAILED);
        return FAULT_HANDLING;
    }

    bt_le_scan_cb_register(&scan_callbacks);
    return SYNCING;
}

static state syncing() {
    int err;

    bt_le_per_adv_sync_cb_register(&sync_callbacks);
    err = bt_le_scan_start(&scan_param, NULL);

    if (err) {
        LOG_ERR(INFO "Failed to start scanning for sync %d", err);
        atomic_set(&fault_reason, BLE_SCAN_START_FAILED);
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

static state handle_fault() {
    LOG_ERR(INFO "Handling fault %ld", atomic_get(&fault_reason));
    k_sleep(K_SECONDS(10));
    sys_reboot(SYS_REBOOT_COLD);
}

static state registering() {
    struct bt_le_per_adv_sync_param sync_create_param;
    struct bt_le_per_adv_sync_info info;
    struct bt_le_per_adv_sync *sync;
    int err;

    k_sem_take(&register_evt_sem, K_FOREVER);
    k_sleep(K_SECONDS(10));
    sync_callbacks.recv = &confirm_recv_cb;
    bt_le_per_adv_sync_get_info(default_sync, &info);

    bt_addr_le_copy(&sync_create_param.addr, &info.addr);
    sync_create_param.options = 0;
    sync_create_param.sid = info.sid;
    sync_create_param.skip = 1;
    sync_create_param.timeout =
        SCALE_INTERVAL_TO_TIMEOUT(info.interval) * CONFIG_NUM_FAILED_SYNC;
    LOG_INF(INFO "Establisehd sync interval %d", info.interval);

    bt_le_per_adv_sync_delete(default_sync);
    err = bt_le_per_adv_sync_create(&sync_create_param, &sync);

    if (err) {
        LOG_WRN(INFO "Failed to recreate sync (err: %d)", err);
        return FAULT_HANDLING;
    }

    k_sem_take(&synced_evt_sem, K_FOREVER);
    fault curr = atomic_get(&fault_reason);
    if (curr != BLE_SYNC_DELETED)
        return FAULT_HANDLING;
    return CONFIRMING;
}

static state confirming() { k_sleep(K_FOREVER); }

static state synced() {
    for (;;) {
        if (k_sem_take(&synced_evt_sem, K_SECONDS(30))) {
            LOG_INF(INFO "Still alive");
            continue;
        }
        fault curr = atomic_get(&fault_reason);

        k_sem_init(&synced_evt_sem, 0, 1);
        switch (curr) {
        case NO_FAULT:
            continue;
        case BLE_SYNC_TIMEOUT:
            return SYNCING;
        default:
            return FAULT_HANDLING;
        }
    }
    return NUM_STATES;
}

static state_func *const states[NUM_STATES] = {
    &init, &handle_fault, &syncing, &registering, &confirming, &synced};

static state run_state() { return states[curr_state](); }

static char *state_str(state s) {
    switch (s) {
    case INITIALIZE:
        return "INITIALIZE";
    case FAULT_HANDLING:
        return "FAULT_HANDLING";
    case SYNCING:
        return "SYNCING";
    case SYNCED:
        return "SYNCED";
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
