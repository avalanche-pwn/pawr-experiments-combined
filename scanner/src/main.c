/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>

#define NAME_LEN 30

static K_SEM_DEFINE(sem_per_adv, 0, 1);
static K_SEM_DEFINE(sem_per_sync, 0, 1);
static K_SEM_DEFINE(sem_per_sync_lost, 0, 1);

static struct bt_conn *default_conn;
static struct bt_le_per_adv_sync *default_sync;
static struct __packed {
    uint8_t subevent;
    uint8_t response_slot;

} pawr_timing;

static void sync_cb(struct bt_le_per_adv_sync *sync,
                    struct bt_le_per_adv_sync_synced_info *info) {
    struct bt_le_per_adv_sync_subevent_params params;
    uint8_t subevents[1];
    char le_addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
    printk("Synced to %s with %d subevents\n", le_addr, info->num_subevents);

    default_sync = sync;

    params.properties = 0;
    params.num_subevents = 1;
    params.subevents = subevents;
    subevents[0] = pawr_timing.subevent;

    err = bt_le_per_adv_sync_subevent(sync, &params);
    if (err) {
        printk("Failed to set subevents to sync to (err %d)\n", err);
    } else {
        printk("Changed sync to subevent %d\n", subevents[0]);
    }

    k_sem_give(&sem_per_sync);
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

    printk("Got scan not sure if ext adv\n");

    // Only process Extended ADV with Periodic ADV info (SyncInfo)
    if (!(info->adv_props & BT_GAP_ADV_PROP_EXT_ADV)) {
        return; // Not Extended ADV
    }

    if (info->interval == 0) {
        // Extended ADV without SyncInfo - keep scanning
        return;
    }

    printk("Received adv packet with sync info, processing...");

    bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));

    bt_addr_le_copy(&sync_create_param.addr, info->addr);
    sync_create_param.options = 0;
    sync_create_param.sid = info->sid;
    sync_create_param.skip = 1;
    sync_create_param.timeout = 1000; // 10 seconds

    err = bt_le_per_adv_sync_create(&sync_create_param, &sync);

    if (err) {
        printk("[Failed to create sync to %s (err %d)\n", addr_str, err);
        return;
    }

    printk("Creating sync to %s (SID=%u)...\n", addr_str, info->sid);
    err = bt_le_scan_stop();
    if (err) {
        printk("Couldn't stop le scanning\n");
        return;
    }
    printk("Stopped le scanning");
}

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv_cb,
    .timeout = NULL,
};

static void term_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_term_info *info) {
    char le_addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

    printk("Sync terminated (reason %d)\n", info->reason);

    default_sync = NULL;

    k_sem_give(&sem_per_sync_lost);
}

static bool print_ad_field(struct bt_data *data, void *user_data) {
    ARG_UNUSED(user_data);

    printk("    0x%02X: ", data->type);
    for (size_t i = 0; i < data->data_len; i++) {
        printk("%02X", data->data[i]);
    }

    printk("\n");

    return true;
}

int bt_le_per_adv_set_response_data(
    struct bt_le_per_adv_sync *per_adv_sync,
    const struct bt_le_per_adv_response_params *params,
    const struct net_buf_simple *data);

static struct bt_le_per_adv_response_params rsp_params;

NET_BUF_SIMPLE_DEFINE_STATIC(rsp_buf, 247);

static void recv_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_recv_info *info,
                    struct net_buf_simple *buf) {
    int err;

    if (buf && buf->len) {
        /* Echo the data back to the advertiser */
        net_buf_simple_reset(&rsp_buf);
        net_buf_simple_add_mem(&rsp_buf, buf->data, buf->len);

        rsp_params.request_event = info->periodic_event_counter;
        rsp_params.request_subevent = info->subevent;
        /* Respond in current subevent and assigned response slot */
        rsp_params.response_subevent = info->subevent;
        rsp_params.response_slot = pawr_timing.response_slot;

        printk("Indication: subevent %d, responding in slot %d\n",
               info->subevent, pawr_timing.response_slot);
        bt_data_parse(buf, print_ad_field, NULL);

        err = bt_le_per_adv_set_response_data(sync, &rsp_params, &rsp_buf);
        if (err) {
            printk("Failed to send response (err %d)\n", err);
        }
    } else if (buf) {
        printk("Received empty indication: subevent %d\n", info->subevent);
    } else {
        printk("Failed to receive indication: subevent %d\n", info->subevent);
    }
}

static struct bt_le_per_adv_sync_cb sync_callbacks = {
    .synced = sync_cb,
    .term = term_cb,
    .recv = recv_cb,
};

static const struct bt_uuid_128 pawr_svc_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0));
static const struct bt_uuid_128 pawr_char_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));

static ssize_t write_timing(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != sizeof(pawr_timing)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(&pawr_timing, buf, len);

    printk("New timing: subevent %d, response slot %d\n", pawr_timing.subevent,
           pawr_timing.response_slot);

    struct bt_le_per_adv_sync_subevent_params params;
    uint8_t subevents[1];
    int err;

    params.properties = 0;
    params.num_subevents = 1;
    params.subevents = subevents;
    subevents[0] = pawr_timing.subevent;

    if (default_sync) {
        err = bt_le_per_adv_sync_subevent(default_sync, &params);
        if (err) {
            printk("Failed to set subevents to sync to (err %d)\n", err);
        } else {
            printk("Changed sync to subevent %d\n", subevents[0]);
        }
    } else {
        printk("Not synced yet\n");
    }

    return len;
}

BT_GATT_SERVICE_DEFINE(pawr_svc, BT_GATT_PRIMARY_SERVICE(&pawr_svc_uuid.uuid),
                       BT_GATT_CHARACTERISTIC(&pawr_char_uuid.uuid,
                                              BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_WRITE, NULL,
                                              write_timing, &pawr_timing));

void connected(struct bt_conn *conn, uint8_t err) {
    printk("Connected, err 0x%02X %s\n", err, bt_hci_err_to_str(err));

    if (err) {
        default_conn = NULL;

        return;
    }

    default_conn = bt_conn_ref(conn);
}

void disconnected(struct bt_conn *conn, uint8_t reason) {
    bt_conn_unref(default_conn);
    default_conn = NULL;

    printk("Disconnected, reason 0x%02X %s\n", reason,
           bt_hci_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected = connected,
    .disconnected = disconnected,
};

static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

#ifndef CONFIG_ARCH_POSIX
#define SLAVE_LED_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(SLAVE_LED_NODE, gpios);

int init_led() {
    if (!gpio_is_ready_dt(&led)) {
        return -1;
    }

    return gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
}
#endif

int main(void) {
    int err;

#ifndef CONFIG_ARCH_POSIX
    init_led();
#endif

    printk("Starting Periodic Advertising with Responses Synchronization "
           "Demo\n");

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);

        return 0;
    }
    printk("Ble up\n");

    bt_le_per_adv_sync_cb_register(&sync_callbacks);
    printk("Registered sync callbacks\n");

    bt_le_scan_cb_register(&scan_callbacks);
    printk("Registered scan callbacks\n");

    // Start active scanning for Extended ADV with Periodic component
    struct bt_le_scan_param scan_param = {
        .type = BT_HCI_LE_SCAN_ACTIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = 0x00A0, // 100 ms
        .window = 0x0050,   // 50 ms
    };

    err = bt_le_scan_start(&scan_param, NULL);
    if (err) {
        printk("Failed to start scanning for sync %d", err);
        return 0;
    }

    do {
        k_sleep(K_SECONDS(30));
        printk("Still in the loop");
    } while (true);

    return 0;
}
