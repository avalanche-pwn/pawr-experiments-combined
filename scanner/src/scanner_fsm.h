#ifndef SCANNER_FSM_H
#define SCANNER_FSM_H

#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>


#ifdef CONFIG_INTERACTIVE
// When building for boards we use the led defined here
// to indicate that this board is a sensor
#define SLAVE_LED_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(SLAVE_LED_NODE, gpios);
#endif


void loop();

#endif // SCANNER_FSM_H
