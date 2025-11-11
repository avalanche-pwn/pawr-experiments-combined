#ifndef SCANNER_FSM_H
#define SCANNER_FSM_H

#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>

typedef enum { INITIALIZE, FAULT_HANDLING, SYNCING, SYNCED, NUM_STATES } state;

typedef enum { NO_FAULT, BLE_ENABLE_FAILED, BLE_SCAN_START_FAILED, BLE_SYNC_TIMEOUT } fault;

typedef state state_func();

#ifdef CONFIG_INTERACTIVE
// When building for boards we use the led defined here
// to indicate that this board is a sensor
#define SLAVE_LED_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(SLAVE_LED_NODE, gpios);
#endif


void loop();

#endif // SCANNER_FSM_H
