#ifndef ADVERTISER_FSM_H
#define ADVERTISER_FSM_H

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

#include <zephyr/drivers/gpio.h>

#include <app/lib/common.h>
#include <app/lib/transfer.h>

#include "advertiser_fsm.h"
#include "free_list.h"

#ifdef CONFIG_INTERACTIVE
#include <app/lib/interactive.h>
#endif // CONFIG_INTERACTIVE

#ifdef CONFIG_INTERACTIVE
#define MASTER_LED_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec master_led =
    GPIO_DT_SPEC_GET(MASTER_LED_NODE, gpios);

#endif // CONFIG_INTERACTIVE

void loop();

#endif // ADVERTISER_FSM_H
