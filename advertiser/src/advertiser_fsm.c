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

#include <zephyr/bluetooth/gap.h>
#include <zephyr/net_buf.h>

#include <app/lib/common.h>
#include <app/lib/transfer.h>

#include "advertiser_fsm.h"
#include "free_list.h"

#ifdef CONFIG_INTERACTIVE
#include <app/lib/interactive.h>
#endif // CONFIG_INTERACTIVE

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
#define FSM "[FSM] "
#define INFO "[INFO] "

#define NUM_RSP_SLOTS 10
#define MAX_NUM_SUBEVENTS 5
#define PACKET_SIZE 5
#define NAME_LEN 30

K_MUTEX_DEFINE(reserve_mutex);
static register_data last_used = {
    .subevent = 0,
    .rsp_slot = CONFIG_NUM_REGISTER_SLOTS
};

static struct bt_le_per_adv_subevent_data_params
    subevent_data_params[MAX_NUM_SUBEVENTS];
static struct net_buf_simple bufs[MAX_NUM_SUBEVENTS];

typedef struct {
    uint16_t dev_id;
    uint8_t inactive_for;
} slot_data;

static slot_data rsp_slots[MAX_NUM_SUBEVENTS][NUM_RSP_SLOTS];
static rsp_data current_rsp;

static struct bt_le_ext_adv *pawr_adv;
subevent_sel_info selection_data;

static uint8_t adv_flags = (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR);
static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_FLAGS, &adv_flags, sizeof(adv_flags)),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, &selection_data, sizeof(selection_data)),
};

static struct bt_le_per_adv_param per_adv_params = {
    .interval_min = 8000,
    .interval_max = 8000,
    .options = 0,
    .num_subevents = MAX_NUM_SUBEVENTS,
    .subevent_interval = 0x40,
    .response_slot_delay = 0x30,
    .response_slot_spacing = 0x2, // Needs to be at least 2
    .num_response_slots = NUM_RSP_SLOTS,
};

register_data register_subevent_data[CONFIG_NUM_REGISTER_SLOTS];

#define TO_SEND_BUF_SIZE                                                       \
    ((NUM_RSP_SLOTS) * 2 + sizeof(register_subevent_data)) +                   \
        sizeof(ack_data) * (NUM_RSP_SLOTS)

static uint8_t backing_store[MAX_NUM_SUBEVENTS][TO_SEND_BUF_SIZE];
static uint8_t counter;

BUILD_ASSERT(ARRAY_SIZE(bufs) == ARRAY_SIZE(subevent_data_params));

static register_data reserve_slot() {
    register_data ret;

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
        register_data sel_slot = reserve_slot();
        register_subevent_data[i] = sel_slot;
    }
    for (size_t i = 0; i < MAX_NUM_SUBEVENTS; i++) {
        net_buf_simple_init_with_data(&bufs[i], &backing_store[i],
                                      TO_SEND_BUF_SIZE);
    }
}

static void populate_reg(struct net_buf_simple *buf) {
    for (size_t i = 0; i < CONFIG_NUM_REGISTER_SLOTS; i++) {
        net_buf_simple_add_mem(buf, &register_subevent_data[i], sizeof(register_subevent_data[i]));
    }
}

static void request_cb(struct bt_le_ext_adv *adv,
                       const struct bt_le_per_adv_data_request *request) {
    int err;
    uint8_t to_send;
    struct net_buf_simple *buf;

    to_send = MIN(request->count, ARRAY_SIZE(subevent_data_params));

    for (size_t i = 0; i < to_send; i++) {
        size_t subevent = (request->start + i) % per_adv_params.num_subevents;
        // Ignore register slots while adding to free list
        net_buf_simple_reset(&bufs[subevent]);
        if (subevent == 0) {
            populate_reg(&bufs[subevent]);
        }
        LOG_INF("StART %d", request->start);

        size_t subevent_start = (subevent == 0) ? CONFIG_NUM_REGISTER_SLOTS : 0;
        size_t data_idx = 0;
        for (size_t j = subevent_start; j < NUM_RSP_SLOTS; j++) {
            slot_data *s = &rsp_slots[subevent][j];
            s->inactive_for++;
            if (s->dev_id != 0 && s->inactive_for > 3) {
                LOG_INF(INFO "Device with id %d, disconnected", s->dev_id);
                s->dev_id = 0;
                s->inactive_for = 0;

                register_data freed = {subevent, j};
                if (free_list_append(freed) != 0) {
                    LOG_WRN(INFO "free list full");
                    // TODO handle this
                }
            }
            ack_data d = {.ack_id = s->dev_id};
            net_buf_simple_add_mem(&bufs[subevent], &d, sizeof(d));
        }
        subevent_data_params[i].subevent =
            (request->start + i) % per_adv_params.num_subevents;
        subevent_data_params[i].response_slot_start = 0;
        subevent_data_params[i].response_slot_count = NUM_RSP_SLOTS;
        subevent_data_params[i].data = &bufs[subevent];
        LOG_INF("asdf %d", bufs[subevent].len);
    }

    err = bt_le_per_adv_set_subevent_data(adv, to_send, subevent_data_params);
    if (err) {
        LOG_WRN(INFO "Failed to set subevent data (err %d)", err);
    } else {
        LOG_INF(INFO "Subevent data set %d", counter);
    }
}

static void response_cb(struct bt_le_ext_adv *adv,
                        struct bt_le_per_adv_response_info *info,
                        struct net_buf_simple *buf) {
    LOG_INF(INFO "Response b: subevent %d, slot %d", info->subevent,
            info->response_slot);
    if (buf) {
        LOG_INF(INFO "Response: subevent %d, slot %d", info->subevent,
                info->response_slot);
        if (buf->len != sizeof(rsp_data)) {
            LOG_WRN(INFO "Invalid data format");
            return;
        }

        memcpy(&current_rsp, buf->data, buf->len);

        if (info->subevent == 0 &&
            info->response_slot < CONFIG_NUM_REGISTER_SLOTS) {
            // registering new device
            register_data d = register_subevent_data[info->response_slot];

            rsp_slots[d.subevent][d.rsp_slot].dev_id = current_rsp.sender_id;
            rsp_slots[d.subevent][d.rsp_slot].inactive_for = 0;
            LOG_INF(INFO "New device, %d %d", d.subevent, d.rsp_slot);

            register_subevent_data[info->response_slot] = reserve_slot();
        }
        slot_data *slot = &rsp_slots[info->subevent][info->response_slot];
        if (slot->dev_id == 0) {
            // slot is empty -> register new device
            LOG_INF(INFO "New device registerd id: %d", current_rsp.sender_id);
            slot->dev_id = current_rsp.sender_id;
            slot->inactive_for = 0;

            // if (selection_data.rsp_slot == NUM_RSP_SLOTS) {
            //     selection_data.subevent++;
            //     selection_data.rsp_slot = 0;
            // } else {
            //     selection_data.rsp_slot++;
            // }

            int err =
                bt_le_ext_adv_set_data(pawr_adv, ad, ARRAY_SIZE(ad), NULL, 0);
            if (err) {
                LOG_ERR(INFO "Failed to set Extended ADV data (err %d)", err);
                // TODO HANDLE THIS
            }

            return;
        } else if (slot->dev_id == current_rsp.sender_id) {
            // Got response from excepted sender
            slot->inactive_for = 0;
            return;
        }
    }
}
static const struct bt_le_ext_adv_cb adv_cb = {
    .pawr_data_request = request_cb,
    .pawr_response = response_cb,
};

// FSM definitions

typedef enum { INITIALIZE, ADVERTISING, FAULT_HANDLING, NUM_STATES } state;
typedef state state_func();

static state init() {
    int err;
    k_sleep(K_SECONDS(5));

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
    per_adv_params.num_subevents = MAX_NUM_SUBEVENTS;
    err = bt_le_per_adv_set_param(pawr_adv, &per_adv_params);
    if (err) {
        LOG_ERR(INFO "Failed to set periodic advertising parameters (err %d)",
                err);
        return FAULT_HANDLING;
    }

    selection_data.num_reg_slots = CONFIG_NUM_REGISTER_SLOTS;

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
