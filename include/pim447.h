#ifndef ZMK__DRIVERS__SENSORS__PIMORONI_PIM447_H
#define ZMK__DRIVERS__SENSORS__PIMORONI_PIM447_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>


struct pimoroni_pim447_config {
    const struct device *i2c_bus;
    uint16_t i2c_addr;
#ifdef CONFIG_ZMK_SENSOR_PIMORONI_PIM447_INTERRUPT
    const struct gpio_dt_spec int_gpio;
#endif

    uint8_t x_input_code;
    uint8_t y_input_code;
};

struct pimoroni_pim447_data {
    const struct device *i2c_dev;
    const struct device *dev;  // Added to store the device pointer
#ifdef CONFIG_ZMK_SENSOR_PIMORONI_PIM447_INTERRUPT
    struct gpio_callback int_gpio_cb;
#endif
    struct k_work work;
    int8_t delta_left;
    int8_t delta_right;
    int8_t delta_up;
    int8_t delta_down;
    bool sw_pressed;
    bool sw_changed;
    
    uint8_t x_input_code;
    uint8_t y_input_code;
};

int pimoroni_pim447_led_set(const struct device *dev, uint8_t red, uint8_t green, uint8_t blue, uint8_t white);
int pimoroni_pim447_set_red(const struct device *dev, uint8_t value);
int pimoroni_pim447_set_green(const struct device *dev, uint8_t value);
int pimoroni_pim447_set_blue(const struct device *dev, uint8_t value);
int pimoroni_pim447_set_white(const struct device *dev, uint8_t value);

#endif /* ZMK__DRIVERS__SENSORS__PIMORONI_PIM447_H */
