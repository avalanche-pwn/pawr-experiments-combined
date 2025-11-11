/*
 * Copyright (c) 2021, Legrand North America, LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <app/lib/interactive.h>

int init_led(struct gpio_dt_spec led) {
    if (!gpio_is_ready_dt(&led)) {
        return -1;
    }

    return gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
}
