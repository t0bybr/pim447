/* SPDX-License-Identifier: MIT */

#ifndef PIMORONI_PIM447_LED_H
#define PIMORONI_PIM447_LED_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>

typedef enum {
    PIM447_LED_RED,
    PIM447_LED_GREEN,
    PIM447_LED_BLUE,
    PIM447_LED_WHITE
} pim447_led_t;

/* Internal helper: set all four channels without taking i2c_lock. Caller must hold data->i2c_lock. */
int set_leds_unlocked(const struct pimoroni_pim447_config *config,
                     uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

void hsv_to_rgbw(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *w);
int pimoroni_pim447_set_led(const struct device *dev, pim447_led_t led, uint8_t brightness);
int pimoroni_pim447_set_leds(const struct device *dev, uint8_t red, uint8_t green, uint8_t blue,
                             uint8_t white);

#endif /* PIMORONI_PIM447_LED_H */
