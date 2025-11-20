#ifndef SCANNER_FSM_H
#define SCANNER_FSM_H

#include <zephyr/kernel.h>

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
#include <zephyr/net_buf.h>

#include <zephyr/drivers/gpio.h>

#include <app/lib/common.h>
#include <app/lib/transfer.h>
#include <app/lib/data_generator.h>

#ifdef CONFIG_INTERACTIVE
#include <app/lib/interactive.h>
#endif


void loop();

#endif // SCANNER_FSM_H
