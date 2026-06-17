/* SPDX-License-Identifier: MIT */
/* behavior_pim447.c - ZMK behavior to adjust PIM447 trackball parameters */

#define DT_DRV_COMPAT zmk_behavior_pim447

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(zmk_behavior_pim447, CONFIG_ZMK_LOG_LEVEL);

#include "pimoroni_pim447.h"
#include "dt-bindings/behavior_pim447.h"

#define PIM447_MAX_SPEED_STEP 1
#define PIM447_MAX_TIME_STEP 1
#define PIM447_SMOOTHING_FACTOR_STEP 0.1f
#define PIM447_HUE_INCREMENT_FACTOR_STEP 0.1f

#define PIM447_MOUSE_MAX_SPEED_MAX 100
#define PIM447_SCROLL_MAX_SPEED_MAX 20
#define PIM447_MOUSE_MAX_TIME_MAX 50
#define PIM447_SCROLL_MAX_TIME_MAX 20
#define PIM447_SMOOTHING_FACTOR_MIN 0.1f
#define PIM447_SMOOTHING_FACTOR_MAX 5.0f
#define PIM447_HUE_INCREMENT_FACTOR_MIN 0.1f
#define PIM447_HUE_INCREMENT_FACTOR_MAX 5.0f

static int behavior_pim447_binding_pressed(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event)
{
    const struct device *pim447_dev = pim447_get_device();
    if (pim447_dev == NULL) {
        LOG_ERR("PIM447 device not ready");
        return -ENODEV;
    }

    uint32_t action = binding->param1;

    k_mutex_lock(&pim447_settings_lock, K_FOREVER);

    switch (action) {
    case PIM447_MOUSE_INC_MAX_SPEED:
        if (pim447_settings.mouse_max_speed < PIM447_MOUSE_MAX_SPEED_MAX) {
            pim447_settings.mouse_max_speed += PIM447_MAX_SPEED_STEP;
        }
        LOG_DBG("Mouse max speed set to %d", pim447_settings.mouse_max_speed);
        break;
    case PIM447_MOUSE_DEC_MAX_SPEED:
        if (pim447_settings.mouse_max_speed > 1) {
            pim447_settings.mouse_max_speed -= PIM447_MAX_SPEED_STEP;
        }
        LOG_DBG("Mouse max speed set to %d", pim447_settings.mouse_max_speed);
        break;
    case PIM447_SCROLL_INC_MAX_SPEED:
        if (pim447_settings.scroll_max_speed < PIM447_SCROLL_MAX_SPEED_MAX) {
            pim447_settings.scroll_max_speed += PIM447_MAX_SPEED_STEP;
        }
        LOG_DBG("Scroll max speed set to %d", pim447_settings.scroll_max_speed);
        break;
    case PIM447_SCROLL_DEC_MAX_SPEED:
        if (pim447_settings.scroll_max_speed > 1) {
            pim447_settings.scroll_max_speed -= PIM447_MAX_SPEED_STEP;
        }
        LOG_DBG("Scroll max speed set to %d", pim447_settings.scroll_max_speed);
        break;
    case PIM447_MOUSE_INC_MAX_TIME:
        if (pim447_settings.mouse_max_time < PIM447_MOUSE_MAX_TIME_MAX) {
            pim447_settings.mouse_max_time += PIM447_MAX_TIME_STEP;
        }
        LOG_DBG("Mouse max time set to %d", pim447_settings.mouse_max_time);
        break;
    case PIM447_MOUSE_DEC_MAX_TIME:
        if (pim447_settings.mouse_max_time > 1) {
            pim447_settings.mouse_max_time -= PIM447_MAX_TIME_STEP;
        }
        LOG_DBG("Mouse max time set to %d", pim447_settings.mouse_max_time);
        break;
    case PIM447_SCROLL_INC_MAX_TIME:
        if (pim447_settings.scroll_max_time < PIM447_SCROLL_MAX_TIME_MAX) {
            pim447_settings.scroll_max_time += PIM447_MAX_TIME_STEP;
        }
        LOG_DBG("Scroll max time set to %d", pim447_settings.scroll_max_time);
        break;
    case PIM447_SCROLL_DEC_MAX_TIME:
        if (pim447_settings.scroll_max_time > 1) {
            pim447_settings.scroll_max_time -= PIM447_MAX_TIME_STEP;
        }
        LOG_DBG("Scroll max time set to %d", pim447_settings.scroll_max_time);
        break;
    case PIM447_MOUSE_INC_SMOOTHING_FACTOR:
        if (pim447_settings.mouse_smoothing_factor < PIM447_SMOOTHING_FACTOR_MAX) {
            pim447_settings.mouse_smoothing_factor += PIM447_SMOOTHING_FACTOR_STEP;
        }
        LOG_DBG("Mouse smoothing factor set to %d.%02d",
                (int)pim447_settings.mouse_smoothing_factor,
                (int)(pim447_settings.mouse_smoothing_factor * 100.0f) % 100);
        break;
    case PIM447_MOUSE_DEC_SMOOTHING_FACTOR:
        if (pim447_settings.mouse_smoothing_factor > PIM447_SMOOTHING_FACTOR_MIN) {
            pim447_settings.mouse_smoothing_factor -= PIM447_SMOOTHING_FACTOR_STEP;
        }
        LOG_DBG("Mouse smoothing factor set to %d.%02d",
                (int)pim447_settings.mouse_smoothing_factor,
                (int)(pim447_settings.mouse_smoothing_factor * 100.0f) % 100);
        break;
    case PIM447_SCROLL_INC_SMOOTHING_FACTOR:
        if (pim447_settings.scroll_smoothing_factor < PIM447_SMOOTHING_FACTOR_MAX) {
            pim447_settings.scroll_smoothing_factor += PIM447_SMOOTHING_FACTOR_STEP;
        }
        LOG_DBG("Scroll smoothing factor set to %d.%02d",
                (int)pim447_settings.scroll_smoothing_factor,
                (int)(pim447_settings.scroll_smoothing_factor * 100.0f) % 100);
        break;
    case PIM447_SCROLL_DEC_SMOOTHING_FACTOR:
        if (pim447_settings.scroll_smoothing_factor > PIM447_SMOOTHING_FACTOR_MIN) {
            pim447_settings.scroll_smoothing_factor -= PIM447_SMOOTHING_FACTOR_STEP;
        }
        LOG_DBG("Scroll smoothing factor set to %d.%02d",
                (int)pim447_settings.scroll_smoothing_factor,
                (int)(pim447_settings.scroll_smoothing_factor * 100.0f) % 100);
        break;
    case PIM447_INC_HUE_INCREMENT_FACTOR:
        if (pim447_settings.hue_increment_factor < PIM447_HUE_INCREMENT_FACTOR_MAX) {
            pim447_settings.hue_increment_factor += PIM447_HUE_INCREMENT_FACTOR_STEP;
        }
        LOG_DBG("Hue increment factor set to %d.%02d",
                (int)pim447_settings.hue_increment_factor,
                (int)(pim447_settings.hue_increment_factor * 100.0f) % 100);
        break;
    case PIM447_DEC_HUE_INCREMENT_FACTOR:
        if (pim447_settings.hue_increment_factor > PIM447_HUE_INCREMENT_FACTOR_MIN) {
            pim447_settings.hue_increment_factor -= PIM447_HUE_INCREMENT_FACTOR_STEP;
        }
        LOG_DBG("Hue increment factor set to %d.%02d",
                (int)pim447_settings.hue_increment_factor,
                (int)(pim447_settings.hue_increment_factor * 100.0f) % 100);
        break;
    case PIM447_TOGGLE_MODE:
        pim447_toggle_mode(pim447_dev);
        break;
    case PIM447_ENABLE_SLEEP:
        pim447_enable_sleep(pim447_dev);
        break;
    case PIM447_DISABLE_SLEEP:
        pim447_disable_sleep(pim447_dev);
        break;
    default:
        LOG_WRN("Unknown trackball adjustment action: %d", action);
        k_mutex_unlock(&pim447_settings_lock);
        return -EINVAL;
    }

    k_mutex_unlock(&pim447_settings_lock);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_pim447_binding_released(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_pim447_driver_api = {
    .binding_pressed = behavior_pim447_binding_pressed,
    .binding_released = behavior_pim447_binding_released,
};

static int behavior_pim447_init(const struct device *dev)
{
    LOG_DBG("PIM447 behavior initialized");
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, behavior_pim447_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_pim447_driver_api);
