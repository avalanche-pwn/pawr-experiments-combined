/*
 * Copyright (c) 2021, Legrand North America, LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/lib/interactive.h>
#include <zephyr/drivers/gpio.h>

int init_led(struct gpio_dt_spec led) {
    if (!gpio_is_ready_dt(&led)) {
        return -1;
    }

    return gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
}
#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

int init_button(void (*cb)(const struct device *dev,
                           struct gpio_callback *cb)) {
    int ret;
    if (!gpio_is_ready_dt(&button)) {
        printk("Error: button device %s is not ready\n", button.port->name);
        return -1;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n", ret,
               button.port->name, button.pin);
        return -1;
    }

    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n", ret,
               button.port->name, button.pin);
        return -1;
    }

    gpio_init_callback(&button_cb_data, cb, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
}
