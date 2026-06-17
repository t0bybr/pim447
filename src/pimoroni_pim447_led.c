/* SPDX-License-Identifier: MIT */
/* pimoroni_pim447_led.c - LED helpers for Pimoroni PIM447 Trackball */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include "pimoroni_pim447.h"
#include "pimoroni_pim447_led.h"

LOG_MODULE_DECLARE(zmk_pimoroni_pim447);

/*
 * Internal helpers that assume the caller already holds data->i2c_lock.
 * The public wrappers below take the lock; nested callers (e.g. sleep
 * control, which performs several I2C ops in one transaction) use these
 * to avoid a non-recursive deadlock on i2c_lock.
 */
static int set_led_unlocked(const struct pimoroni_pim447_config *config,
                            pim447_led_t led, uint8_t brightness)
{
    uint8_t reg;

    switch (led) {
    case PIM447_LED_RED:
        reg = REG_LED_RED;
        break;
    case PIM447_LED_GREEN:
        reg = REG_LED_GRN;
        break;
    case PIM447_LED_BLUE:
        reg = REG_LED_BLU;
        break;
    case PIM447_LED_WHITE:
        reg = REG_LED_WHT;
        break;
    default:
        LOG_ERR("Invalid LED specified");
        return -EINVAL;
    }

    int ret = i2c_reg_write_byte_dt(&config->i2c, reg, brightness);
    if (ret) {
        LOG_ERR("Failed to set LED brightness: %d", ret);
        return ret;
    }

    LOG_DBG("LED %d brightness set to %d", led, brightness);
    return 0;
}

int set_leds_unlocked(const struct pimoroni_pim447_config *config,
                             uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
    uint8_t led_values[4] = { red, green, blue, white };

    int ret = i2c_burst_write_dt(&config->i2c, REG_LED_RED, led_values, sizeof(led_values));
    if (ret) {
        LOG_ERR("Failed to set LED brightness levels: %d", ret);
        return ret;
    }

    return 0;
}

int pimoroni_pim447_set_led(const struct device *dev, pim447_led_t led, uint8_t brightness)
{
    const struct pimoroni_pim447_config *config = dev->config;
    struct pimoroni_pim447_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->i2c_lock, K_FOREVER);
    ret = set_led_unlocked(config, led, brightness);
    k_mutex_unlock(&data->i2c_lock);

    return ret;
}

int pimoroni_pim447_set_leds(const struct device *dev, uint8_t red, uint8_t green, uint8_t blue,
                             uint8_t white)
{
    const struct pimoroni_pim447_config *config = dev->config;
    struct pimoroni_pim447_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->i2c_lock, K_FOREVER);
    ret = set_leds_unlocked(config, red, green, blue, white);
    k_mutex_unlock(&data->i2c_lock);

    return ret;
}

/*
 * HSV to RGBW conversion.
 *
 * The PIM447 has a dedicated white LED. To make use of it, the white
 * channel takes over the common (min) component of the RGB triplet, and
 * that amount is subtracted from R/G/B so the perceived hue is preserved
 * while the white LED carries the brightness. Without the subtraction
 * the previous implementation lit R/G/B fully *and* added white on top,
 * which washed out colours and doubled brightness unpredictably.
 *
 * Output range: 0..255 per channel.
 */
void hsv_to_rgbw(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *w)
{
    /* Normalize hue to [0, 360). */
    if (h < 0.0f) {
        h = fmodf(h, 360.0f) + 360.0f;
    } else if (h >= 360.0f) {
        h = fmodf(h, 360.0f);
    }

    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r_prime, g_prime, b_prime;

    if (h < 60.0f) {
        r_prime = c; g_prime = x; b_prime = 0.0f;
    } else if (h < 120.0f) {
        r_prime = x; g_prime = c; b_prime = 0.0f;
    } else if (h < 180.0f) {
        r_prime = 0.0f; g_prime = c; b_prime = x;
    } else if (h < 240.0f) {
        r_prime = 0.0f; g_prime = x; b_prime = c;
    } else if (h < 300.0f) {
        r_prime = x; g_prime = 0.0f; b_prime = c;
    } else {
        r_prime = c; g_prime = 0.0f; b_prime = x;
    }

    /* White takes the shared component; subtract it from RGB to keep hue. */
    float w_prime = fminf(r_prime, fminf(g_prime, b_prime));
    r_prime -= w_prime;
    g_prime -= w_prime;
    b_prime -= w_prime;

    /* m (value offset) goes entirely to white for brightness. */
    *r = (uint8_t)((r_prime) * 255.0f);
    *g = (uint8_t)((g_prime) * 255.0f);
    *b = (uint8_t)((b_prime) * 255.0f);
    *w = (uint8_t)((w_prime + m) * 255.0f);
}
