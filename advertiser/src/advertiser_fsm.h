#ifndef ADVERTISER_FSM_H
#define ADVERTISER_FSM_H

#include <zephyr/drivers/gpio.h>

#ifdef CONFIG_INTERACTIVE
#define MASTER_LED_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec master_led =
    GPIO_DT_SPEC_GET(MASTER_LED_NODE, gpios);

#endif // CONFIG_INTERACTIVE

void loop();

#endif // ADVERTISER_FSM_H
