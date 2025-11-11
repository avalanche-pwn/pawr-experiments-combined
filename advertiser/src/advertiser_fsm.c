#include <stdint.h>
#include <stdio.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include <zephyr/sys/reboot.h>

#include <app/lib/transfer.h>

#include "advertiser_fsm.h"
#include "zephyr/bluetooth/gap.h"

#ifdef CONFIG_INTERACTIVE
#include <app/lib/interactive.h>
#endif // CONFIG_INTERACTIVE

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
#define FSM "[FSM] "
#define INFO "[INFO] "

#define NUM_RSP_SLOTS 128
#define MAX_NUM_SUBEVENTS 5
#define PACKET_SIZE 5
#define NAME_LEN 30

static uint8_t num_subevents = 1;
static uint8_t last_rsp_slot = 0;

static struct bt_le_per_adv_subevent_data_params
    subevent_data_params[MAX_NUM_SUBEVENTS];
static struct net_buf_simple bufs[MAX_NUM_SUBEVENTS];
static uint8_t backing_store[MAX_NUM_SUBEVENTS][PACKET_SIZE];

static struct bt_le_per_adv_param per_adv_params = {
    .interval_min = 8000,
    .interval_max = 8000,
    .options = 0,
    .num_subevents = MAX_NUM_SUBEVENTS,
    .subevent_interval = 0x30,
    .response_slot_delay = 0x8,
    .response_slot_spacing = 0x2, // Needs to be at least 2
    .num_response_slots = NUM_RSP_SLOTS,
};

static uint8_t counter;

BUILD_ASSERT(ARRAY_SIZE(bufs) == ARRAY_SIZE(subevent_data_params));
BUILD_ASSERT(ARRAY_SIZE(backing_store) == ARRAY_SIZE(subevent_data_params));

void init_bufs(void) {
    for (size_t i = 0; i < ARRAY_SIZE(backing_store); i++) {
        backing_store[i][0] = ARRAY_SIZE(backing_store[i]) - 1;
        backing_store[i][1] = BT_DATA_MANUFACTURER_DATA;
        backing_store[i][2] = 0x59; /* Nordic */
        backing_store[i][3] = 0x00;

        net_buf_simple_init_with_data(&bufs[i], &backing_store[i],
                                      ARRAY_SIZE(backing_store[i]));
    }
}

static void request_cb(struct bt_le_ext_adv *adv,
                       const struct bt_le_per_adv_data_request *request) {
    int err;
    uint8_t to_send;
    struct net_buf_simple *buf;

    to_send = MIN(request->count, ARRAY_SIZE(subevent_data_params));

    for (size_t i = 0; i < to_send; i++) {
        buf = &bufs[i];
        buf->data[buf->len - 1] = counter++;

        subevent_data_params[i].subevent =
            (request->start + i) % per_adv_params.num_subevents;
        subevent_data_params[i].response_slot_start = 0;
        subevent_data_params[i].response_slot_count = NUM_RSP_SLOTS;
        subevent_data_params[i].data = buf;
    }

    err = bt_le_per_adv_set_subevent_data(adv, to_send, subevent_data_params);
    if (err) {
        LOG_WRN(INFO "Failed to set subevent data (err %d)", err);
    } else {
        LOG_INF(INFO "Subevent data set %d", counter);
    }
}

static bool print_ad_field(struct bt_data *data, void *user_data) {
    ARG_UNUSED(user_data);

    // printk("    0x%02X: ", data->type);
    // for (size_t i = 0; i < data->data_len; i++) {
    //     printk("%02X", data->data[i]);
    // }

    // printk("\n");

    return true;
}

static void response_cb(struct bt_le_ext_adv *adv,
                        struct bt_le_per_adv_response_info *info,
                        struct net_buf_simple *buf) {
    if (buf) {
        LOG_INF(INFO "Response: subevent %d, slot %d", info->subevent,
                info->response_slot);
        bt_data_parse(buf, print_ad_field, NULL);
    }
}
static const struct bt_le_ext_adv_cb adv_cb = {
    .pawr_data_request = request_cb,
    .pawr_response = response_cb,
};

// FSM definitions

typedef enum { INITIALIZE, ADVERTISING, FAULT_HANDLING, NUM_STATES } state;
typedef state state_func();

subevent_sel_info selection_data;

static state init() {
    int err;
    k_sleep(K_SECONDS(5));
    struct bt_le_ext_adv *pawr_adv;
#ifdef CONFIG_INTERACTIVE
    init_led(master_led);
#endif // CONFIG_INTERACTIVE
    init_bufs();

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
    per_adv_params.num_subevents = num_subevents;
    err = bt_le_per_adv_set_param(pawr_adv, &per_adv_params);
    if (err) {
        LOG_ERR(INFO "Failed to set periodic advertising parameters (err %d)",
                err);
        return FAULT_HANDLING;
    }

    static uint8_t adv_flags = (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR);
    selection_data.subevent = num_subevents - 1;
    selection_data.rsp_slot = last_rsp_slot + 1;
    static const struct bt_data ad[] =
        {
            BT_DATA(BT_DATA_FLAGS, &adv_flags, sizeof(adv_flags)),
            BT_DATA(BT_DATA_MANUFACTURER_DATA, &selection_data, sizeof(selection_data)),
        };

    err = bt_le_ext_adv_set_data(pawr_adv, ad, ARRAY_SIZE(ad), NULL, 0);
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

static state advertising() {
    while (true) {
        k_sleep(K_SECONDS(10));
        LOG_INF(INFO "Still alive");
    }
    return NUM_STATES;
}

static state fault_handling() {
    LOG_ERR(INFO "Received a fault rebooting");
    sys_reboot(SYS_REBOOT_COLD);
}

static const char *state_str(state s) {
    switch (s) {
    case INITIALIZE:
        return "INITIALIZE";
    case ADVERTISING:
        return "ADVERTISING";
    default:
        return "INVALID_STATE";
    }
}

static state curr_state = INITIALIZE;
state_func *const states[NUM_STATES] = {&init, &advertising, &fault_handling};

static state run_state() { return states[curr_state](); }

void loop() {
    for (;;) {
        LOG_INF(FSM "Transitioning to state %s", state_str(curr_state));
        curr_state = run_state();
    }
}
