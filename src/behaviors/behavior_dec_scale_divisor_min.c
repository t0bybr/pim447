/* behavior_dec_scale_divisor_min.c */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Extern variables */
extern volatile float scale_divisor_min;
extern struct k_mutex variable_mutex;

/* Constants for limits */
#define SCALE_DIVISOR_MIN_MIN 1.0f
#define SCALE_DIVISOR_MIN_MAX 5.0f
#define SCALE_DIVISOR_STEP 0.1f

/* Decrease function */
static int on_dec_scale_divisor_min(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event)
{
    if (event.position != ZMK_POSITION_STATE_RELEASED) {
        k_mutex_lock(&variable_mutex, K_FOREVER);

        scale_divisor_min -= SCALE_DIVISOR_STEP;
        if (scale_divisor_min < SCALE_DIVISOR_MIN_MIN) {
            scale_divisor_min = SCALE_DIVISOR_MIN_MIN;
        }

        LOG_INF("scale_divisor_min decreased to %f", (double)scale_divisor_min);

        k_mutex_unlock(&variable_mutex);
    }
    return 0;
}

/* Behavior driver API */
static const struct behavior_driver_api behavior_dec_api = {
    .binding_pressed = on_dec_scale_divisor_min,
};

/* Device registration */
DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL,
                      APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                      &behavior_dec_api);
