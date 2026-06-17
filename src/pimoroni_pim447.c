/* SPDX-License-Identifier: MIT */
/* pimoroni_pim447.c - Driver for Pimoroni PIM447 Trackball */

#define DT_DRV_COMPAT zmk_pimoroni_pim447

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zmk/events/activity_state_changed.h>
#include <math.h>

#include "pimoroni_pim447.h"
#include "pimoroni_pim447_led.h"

LOG_MODULE_REGISTER(zmk_pimoroni_pim447, CONFIG_ZMK_PIMORONI_PIM447_LOG_LEVEL);

/* Statically initialized so it is safe to use even if the driver init fails. */
struct k_mutex pim447_settings_lock;

struct pim447_settings pim447_settings = {
    .mouse_max_speed = 25,
    .mouse_max_time = 5,
    .mouse_smoothing_factor = 1.3f,
    .scroll_max_speed = 1,
    .scroll_max_time = 1,
    .scroll_smoothing_factor = 0.5f,
    .hue_increment_factor = 0.3f,
};

#define AUTOMOUSE_LAYER (DT_PROP(DT_DRV_INST(0), automouse_layer))

#if AUTOMOUSE_LAYER > 0
static bool automouse_triggered;
#endif

/* Forward declarations */
static void pimoroni_pim447_gpio_callback(const struct device *port, struct gpio_callback *cb,
                                          gpio_port_pins_t pins);
static int pimoroni_pim447_enable_interrupt(const struct pimoroni_pim447_config *config,
                                            struct pimoroni_pim447_data *data, bool enable);
#if AUTOMOUSE_LAYER > 0
static void activate_automouse_layer(void);
static void deactivate_automouse_layer(struct k_timer *timer);
#else
static inline void activate_automouse_layer(void) {}
#endif

const struct device *pim447_get_device(void)
{
    const struct device *dev = DEVICE_DT_GET(DT_INST(0, zmk_pimoroni_pim447));

    if (!device_is_ready(dev)) {
        return NULL;
    }

    return dev;
}

/*
 * Sleep enable/disable run from the ZMK activity listener thread and race
 * against the workqueue's I2C traffic. Take i2c_lock for the whole sequence
 * so the CTRL register update and the LED shutdown are atomic with respect
 * to movement reads. The LED write uses the _unlocked helper to avoid a
 * recursive lock.
 */
void pim447_enable_sleep(const struct device *dev)
{
    const struct pimoroni_pim447_config *config = dev->config;
    struct pimoroni_pim447_data *data = dev->data;
    uint8_t ctrl_reg_value;

    k_mutex_lock(&data->i2c_lock, K_FOREVER);

    if (i2c_reg_read_byte_dt(&config->i2c, REG_CTRL, &ctrl_reg_value) != 0) {
        LOG_ERR("Failed to read PIM447 control register");
        k_mutex_unlock(&data->i2c_lock);
        return;
    }

    ctrl_reg_value |= MSK_CTRL_SLEEP;

    if (i2c_reg_write_byte_dt(&config->i2c, REG_CTRL, ctrl_reg_value) != 0) {
        LOG_ERR("Failed to write PIM447 control register");
        k_mutex_unlock(&data->i2c_lock);
        return;
    }

    set_leds_unlocked(config, 0, 0, 0, 0);

    k_mutex_unlock(&data->i2c_lock);

    LOG_DBG("PIM447 sleep enabled");
}

void pim447_disable_sleep(const struct device *dev)
{
    const struct pimoroni_pim447_config *config = dev->config;
    struct pimoroni_pim447_data *data = dev->data;
    uint8_t ctrl_reg_value;

    k_mutex_lock(&data->i2c_lock, K_FOREVER);

    if (i2c_reg_read_byte_dt(&config->i2c, REG_CTRL, &ctrl_reg_value) != 0) {
        LOG_ERR("Failed to read PIM447 control register");
        k_mutex_unlock(&data->i2c_lock);
        return;
    }

    ctrl_reg_value &= ~MSK_CTRL_SLEEP;

    if (i2c_reg_write_byte_dt(&config->i2c, REG_CTRL, ctrl_reg_value) != 0) {
        LOG_ERR("Failed to write PIM447 control register");
        k_mutex_unlock(&data->i2c_lock);
        return;
    }

    set_leds_unlocked(config, 255, 0, 0, 0);

    k_mutex_unlock(&data->i2c_lock);

    LOG_DBG("PIM447 sleep disabled");
}

void pim447_toggle_mode(const struct device *dev)
{
    struct pimoroni_pim447_data *data = dev->data;

    k_mutex_lock(&data->data_lock, K_FOREVER);
    if (data->mode == PIM447_MODE_MOUSE) {
        data->mode = PIM447_MODE_SCROLL;
    } else {
        data->mode = PIM447_MODE_MOUSE;
    }
    enum pim447_mode new_mode = data->mode;

    /* Reset smoothing state on mode switch to avoid a stale first sample. */
    data->previous_x = 0;
    data->previous_y = 0;
    data->smoothed_x = 0;
    data->smoothed_y = 0;
    k_mutex_unlock(&data->data_lock);

    LOG_DBG("PIM447 mode switched to %s",
            (new_mode == PIM447_MODE_MOUSE) ? "MOUSE" : "SCROLL");
}

static int activity_state_changed_handler(const zmk_event_t *eh)
{
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    const struct device *dev = pim447_get_device();

    if (dev == NULL) {
        LOG_ERR("PIM447 device not ready");
        return -ENODEV;
    }

    if (ev->state == ZMK_ACTIVITY_IDLE) {
        pim447_enable_sleep(dev);
    } else {
        pim447_disable_sleep(dev);
    }

    return 0;
}

ZMK_LISTENER(idle_listener, activity_state_changed_handler);
ZMK_SUBSCRIPTION(idle_listener, zmk_activity_state_changed);

static void pim447_process_movement(struct pimoroni_pim447_data *data, int delta_x,
                                    int delta_y, uint32_t time_between_interrupts,
                                    int max_speed, int max_time, float smoothing_factor)
{
    float scaling_factor = 1.0f;

    if (time_between_interrupts < (uint32_t)max_time) {
        float exponent = -3.0f * (float)time_between_interrupts / (float)max_time;
        scaling_factor = 1.0f + (max_speed - 1.0f) * expf(exponent);
    }

    k_mutex_lock(&data->data_lock, K_FOREVER);
    if (data->mode == PIM447_MODE_SCROLL) {
        scaling_factor *= 2.5f;
    }
    k_mutex_unlock(&data->data_lock);

    int scaled_x_movement = (int)(delta_x * scaling_factor);
    int scaled_y_movement = (int)(delta_y * scaling_factor);

    k_mutex_lock(&data->data_lock, K_FOREVER);
    data->smoothed_x =
        (int)(smoothing_factor * scaled_x_movement + (1.0f - smoothing_factor) * data->previous_x);
    data->smoothed_y =
        (int)(smoothing_factor * scaled_y_movement + (1.0f - smoothing_factor) * data->previous_y);

    data->previous_x = data->smoothed_x;
    data->previous_y = data->smoothed_y;
    k_mutex_unlock(&data->data_lock);
}

/*
 * All I2C traffic in the work handler is serialized under i2c_lock so it
 * cannot collide with sleep/wake writes from the activity listener thread
 * or with LED writes from other contexts.
 */
static void pimoroni_pim447_work_handler(struct k_work *work)
{
    struct pimoroni_pim447_data *data = CONTAINER_OF(work, struct pimoroni_pim447_data, irq_work);
    const struct pimoroni_pim447_config *config = data->dev->config;
    const struct device *dev = data->dev;
    uint8_t buf[5];
    int ret;

    LOG_DBG("PIM447 work handler triggered");

    k_mutex_lock(&data->i2c_lock, K_FOREVER);

    ret = i2c_burst_read_dt(&config->i2c, REG_LEFT, buf, sizeof(buf));
    if (ret) {
        LOG_ERR("Failed to read movement data from PIM447: %d", ret);
        k_mutex_unlock(&data->i2c_lock);
        return;
    }

    /* Clear movement registers and the interrupt flag while we still hold the lock. */
    i2c_reg_write_byte_dt(&config->i2c, REG_LEFT, 0);
    i2c_reg_write_byte_dt(&config->i2c, REG_RIGHT, 0);
    i2c_reg_write_byte_dt(&config->i2c, REG_UP, 0);
    i2c_reg_write_byte_dt(&config->i2c, REG_DOWN, 0);

    uint8_t int_status;
    ret = i2c_reg_read_byte_dt(&config->i2c, REG_INT, &int_status);
    if (ret == 0 && (int_status & MSK_INT_TRIGGERED)) {
        int_status &= ~MSK_INT_TRIGGERED;
        i2c_reg_write_byte_dt(&config->i2c, REG_INT, int_status);
    }

    k_mutex_unlock(&data->i2c_lock);

    uint32_t time_between_interrupts;
    enum pim447_mode mode;

    k_mutex_lock(&data->data_lock, K_FOREVER);
    time_between_interrupts = data->last_interrupt_time - data->previous_interrupt_time;
    mode = data->mode;
    k_mutex_unlock(&data->data_lock);

    int16_t delta_x = (int16_t)buf[1] - (int16_t)buf[0]; /* RIGHT - LEFT */
    int16_t delta_y = (int16_t)buf[3] - (int16_t)buf[2]; /* DOWN - UP */

    if (delta_x != 0 || delta_y != 0) {
        int max_speed, max_time;
        float smoothing_factor;

        k_mutex_lock(&pim447_settings_lock, K_FOREVER);
        if (mode == PIM447_MODE_MOUSE) {
            max_speed = pim447_settings.mouse_max_speed;
            max_time = pim447_settings.mouse_max_time;
            smoothing_factor = pim447_settings.mouse_smoothing_factor;
        } else {
            max_speed = pim447_settings.scroll_max_speed;
            max_time = pim447_settings.scroll_max_time;
            smoothing_factor = pim447_settings.scroll_smoothing_factor;
        }
        k_mutex_unlock(&pim447_settings_lock);

        pim447_process_movement(data, delta_x, delta_y, time_between_interrupts, max_speed,
                                max_time, smoothing_factor);

        if (mode == PIM447_MODE_MOUSE) {
            if (data->smoothed_x != 0) {
                ret = input_report_rel(dev, INPUT_REL_X, data->smoothed_x, true, K_NO_WAIT);
                if (ret) {
                    LOG_ERR("Failed to report delta_x: %d", ret);
                } else {
                    LOG_DBG("Reported delta_x: %d", data->smoothed_x);
                }
            }
            if (data->smoothed_y != 0) {
                ret = input_report_rel(dev, INPUT_REL_Y, data->smoothed_y, true, K_NO_WAIT);
                if (ret) {
                    LOG_ERR("Failed to report delta_y: %d", ret);
                } else {
                    LOG_DBG("Reported delta_y: %d", data->smoothed_y);
                }
            }
        } else {
            if (data->smoothed_x != 0) {
                ret = input_report_rel(dev, INPUT_REL_WHEEL, data->smoothed_x, true, K_NO_WAIT);
                if (ret) {
                    LOG_ERR("Failed to report wheel: %d", ret);
                } else {
                    LOG_DBG("Reported wheel: %d", data->smoothed_x);
                }
            }
            if (data->smoothed_y != 0) {
                ret = input_report_rel(dev, INPUT_REL_HWHEEL, data->smoothed_y, true, K_NO_WAIT);
                if (ret) {
                    LOG_ERR("Failed to report hwheel: %d", ret);
                } else {
                    LOG_DBG("Reported hwheel: %d", data->smoothed_y);
                }
            }
        }
    } else {
        /* No movement: decay the smoothing state so the next motion starts clean. */
        k_mutex_lock(&data->data_lock, K_FOREVER);
        data->previous_x = 0;
        data->previous_y = 0;
        k_mutex_unlock(&data->data_lock);
    }

    data->sw_pressed = (buf[4] & MSK_SWITCH_STATE) != 0;

    if (data->sw_pressed != data->sw_pressed_prev) {
        ret = input_report_key(dev, INPUT_BTN_0, data->sw_pressed ? 1 : 0, true, K_NO_WAIT);
        if (ret) {
            LOG_ERR("Failed to report key: %d", ret);
        } else {
            LOG_DBG("Reported switch state: %d", data->sw_pressed);
        }
        data->sw_pressed_prev = data->sw_pressed;
    }

    float speed = 0.0f;
    if (delta_x != 0 || delta_y != 0) {
        speed = sqrtf((float)(delta_x * delta_x + delta_y * delta_y));
    }

    if (speed > 0.0f) {
        activate_automouse_layer();

        k_mutex_lock(&pim447_settings_lock, K_FOREVER);
        float hue_increment = pim447_settings.hue_increment_factor;
        k_mutex_unlock(&pim447_settings_lock);

        k_mutex_lock(&data->data_lock, K_FOREVER);
        data->hue += speed * hue_increment;
        if (data->hue >= 360.0f) {
            data->hue -= 360.0f;
        }
        float hue = data->hue;
        k_mutex_unlock(&data->data_lock);

        uint8_t r, g, b, w;
        hsv_to_rgbw(hue, 1.0f, 1.0f, &r, &g, &b, &w);

        ret = pimoroni_pim447_set_leds(dev, r, g, b, w);
        if (ret) {
            LOG_ERR("Failed to set LEDs: %d", ret);
        }
    }
}

/*
 * GPIO ISR: runs in interrupt context. k_mutex_* is not ISR-safe, so the
 * timestamp pair is updated under irq_lock instead. The work handler reads
 * them back under data_lock in thread context; irq_lock provides the
 * necessary memory barrier / atomicity against that read.
 */
static void pimoroni_pim447_gpio_callback(const struct device *port, struct gpio_callback *cb,
                                          gpio_port_pins_t pins)
{
    struct pimoroni_pim447_data *data = CONTAINER_OF(cb, struct pimoroni_pim447_data, int_gpio_cb);
    uint32_t current_time = k_uptime_get_32();

    unsigned int key = irq_lock();
    data->previous_interrupt_time = data->last_interrupt_time;
    data->last_interrupt_time = current_time;
    irq_unlock(key);

    k_work_submit(&data->irq_work);
}

static int pimoroni_pim447_enable_interrupt(const struct pimoroni_pim447_config *config,
                                            struct pimoroni_pim447_data *data, bool enable)
{
    uint8_t int_reg;
    int ret;

    k_mutex_lock(&data->i2c_lock, K_FOREVER);

    ret = i2c_reg_read_byte_dt(&config->i2c, REG_INT, &int_reg);
    if (ret) {
        LOG_ERR("Failed to read INT register");
        k_mutex_unlock(&data->i2c_lock);
        return ret;
    }

    LOG_DBG("INT register before changing: 0x%02X", int_reg);

    if (enable) {
        int_reg |= MSK_INT_OUT_EN;
    } else {
        int_reg &= ~MSK_INT_OUT_EN;
    }

    ret = i2c_reg_write_byte_dt(&config->i2c, REG_INT, int_reg);
    if (ret) {
        LOG_ERR("Failed to write INT register");
        k_mutex_unlock(&data->i2c_lock);
        return ret;
    }

    k_mutex_unlock(&data->i2c_lock);

    LOG_DBG("INT register after changing: 0x%02X", int_reg);

    return 0;
}

static int pimoroni_pim447_enable(const struct device *dev)
{
    const struct pimoroni_pim447_config *config = dev->config;
    struct pimoroni_pim447_data *data = dev->data;
    int ret;

    LOG_DBG("pimoroni_pim447_enable called");

    if (!device_is_ready(config->int_gpio.port)) {
        LOG_ERR("Interrupt GPIO device is not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT | GPIO_PULL_UP);
    if (ret) {
        LOG_ERR("Failed to configure interrupt GPIO");
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_FALLING);
    if (ret) {
        LOG_ERR("Failed to configure GPIO interrupt");
        return ret;
    }

    gpio_init_callback(&data->int_gpio_cb, pimoroni_pim447_gpio_callback,
                       BIT(config->int_gpio.pin));

    ret = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
    if (ret) {
        LOG_ERR("Failed to add GPIO callback");
        return ret;
    }

    ret = pimoroni_pim447_enable_interrupt(config, data, true);
    if (ret) {
        LOG_ERR("Failed to enable interrupt output");
        return ret;
    }

    LOG_DBG("pimoroni_pim447 enabled");
    return 0;
}

static int pimoroni_pim447_init(const struct device *dev)
{
    const struct pimoroni_pim447_config *config = dev->config;
    struct pimoroni_pim447_data *data = dev->data;
    int ret;

    LOG_INF("PIM447 driver initializing");

    data->dev = dev;
    data->sw_pressed_prev = false;
    data->mode = PIM447_MODE_MOUSE;

    k_mutex_init(&data->data_lock);
    k_mutex_init(&data->i2c_lock);

    if (!device_is_ready(config->i2c.bus)) {
        LOG_ERR("I2C bus device is not ready");
        return -ENODEV;
    }

    k_mutex_lock(&data->i2c_lock, K_FOREVER);

    uint8_t chip_id_l, chip_id_h;
    ret = i2c_reg_read_byte_dt(&config->i2c, REG_CHIP_ID_L, &chip_id_l);
    if (ret) {
        LOG_ERR("Failed to read chip ID low byte: %d", ret);
        k_mutex_unlock(&data->i2c_lock);
        return ret;
    }

    ret = i2c_reg_read_byte_dt(&config->i2c, REG_CHIP_ID_H, &chip_id_h);
    if (ret) {
        LOG_ERR("Failed to read chip ID high byte: %d", ret);
        k_mutex_unlock(&data->i2c_lock);
        return ret;
    }

    k_mutex_unlock(&data->i2c_lock);

    uint16_t chip_id = ((uint16_t)chip_id_h << 8) | chip_id_l;
    LOG_INF("PIM447 chip ID: 0x%04X", chip_id);
    if (chip_id != PIM447_CHIP_ID_EXPECTED) {
        LOG_ERR("Unexpected chip ID 0x%04X (expected 0x%04X) - not a PIM447?",
                chip_id, PIM447_CHIP_ID_EXPECTED);
        return -ENODEV;
    }

    ret = pimoroni_pim447_enable(dev);
    if (ret) {
        LOG_ERR("Failed to enable PIM447");
        return ret;
    }

    k_work_init(&data->irq_work, pimoroni_pim447_work_handler);

    LOG_INF("PIM447 driver initialized");
    return 0;
}

#if AUTOMOUSE_LAYER > 0
static void activate_automouse_layer(void)
{
    automouse_triggered = true;
    zmk_keymap_layer_activate(AUTOMOUSE_LAYER);
    k_timer_start(&automouse_layer_timer, K_MSEC(CONFIG_ZMK_PIMORONI_PIM447_AUTOMOUSE_TIMEOUT_MS),
                  K_NO_WAIT);
}

static void deactivate_automouse_layer(struct k_timer *timer)
{
    automouse_triggered = false;
    zmk_keymap_layer_deactivate(AUTOMOUSE_LAYER);
}

K_TIMER_DEFINE(automouse_layer_timer, deactivate_automouse_layer, NULL);
#endif

static const struct pimoroni_pim447_config pimoroni_pim447_config = {
    .i2c = I2C_DT_SPEC_INST_GET(0),
    .int_gpio = GPIO_DT_SPEC_INST_GET(0, int_gpios),
};

static struct pimoroni_pim447_data pimoroni_pim447_data;

DEVICE_DT_INST_DEFINE(0, pimoroni_pim447_init, NULL, &pimoroni_pim447_data,
                      &pimoroni_pim447_config, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);
