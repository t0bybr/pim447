/* SPDX-License-Identifier: MIT */

#ifndef PIMORONI_PIM447_H
#define PIMORONI_PIM447_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/mutex.h>

/* Register Addresses */
#define REG_LED_RED     0x00
#define REG_LED_GRN     0x01
#define REG_LED_BLU     0x02
#define REG_LED_WHT     0x03
#define REG_LEFT        0x04
#define REG_RIGHT       0x05
#define REG_UP          0x06
#define REG_DOWN        0x07
#define REG_SWITCH      0x08
#define REG_USER_FLASH  0xD0
#define REG_FLASH_PAGE  0xF0
#define REG_INT         0xF9
#define REG_CHIP_ID_L   0xFA
#define REG_CHIP_ID_H   0xFB
#define REG_VERSION     0xFC
#define REG_I2C_ADDR    0xFD
#define REG_CTRL        0xFE

/* Bit Masks */
#define MSK_SWITCH_STATE    0b10000000

/* Interrupt Masks */
#define MSK_INT_TRIGGERED   0b00000001
#define MSK_INT_OUT_EN      0b00000010

/* Control Masks */
#define MSK_CTRL_SLEEP      0b00000001
#define MSK_CTRL_RESET      0b00000010

#define LED_ANIMATION_INTERVAL_MS 50

/* Expected chip ID of the PIM447 trackball breakout (little-endian in regs 0xFA/0xFB). */
#define PIM447_CHIP_ID_EXPECTED 0xBA11

enum pim447_mode {
    PIM447_MODE_MOUSE,
    PIM447_MODE_SCROLL
};

struct pimoroni_pim447_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
};

struct pimoroni_pim447_data {
    const struct device *dev;
    struct gpio_callback int_gpio_cb;
    struct k_work irq_work;
    struct k_mutex data_lock;
    struct k_mutex i2c_lock;
    enum pim447_mode mode;
    float hue;
    bool sw_pressed;
    bool sw_pressed_prev;
    int previous_x;
    int previous_y;
    int smoothed_x;
    int smoothed_y;
    uint32_t last_interrupt_time;
    uint32_t previous_interrupt_time;
};

/*
 * Tunable parameters shared with the behavior layer.
 * Protected by pim447_settings_lock, which is statically initialized.
 */
struct pim447_settings {
    uint8_t mouse_max_speed;
    uint8_t mouse_max_time;
    float mouse_smoothing_factor;
    uint8_t scroll_max_speed;
    uint8_t scroll_max_time;
    float scroll_smoothing_factor;
    float hue_increment_factor;
};

extern struct k_mutex pim447_settings_lock;
extern struct pim447_settings pim447_settings;

const struct device *pim447_get_device(void);
void pim447_enable_sleep(const struct device *dev);
void pim447_disable_sleep(const struct device *dev);
void pim447_toggle_mode(const struct device *dev);

#endif /* PIMORONI_PIM447_H */
