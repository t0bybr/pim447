/* behavior_dec_speed_max.c */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Extern variables */
extern volatile float speed_max;
extern struct k_mutex variable_mutex;

/* Constants for limits */
#define SPEED_MAX_MIN 0.1f
#define SPEED_MAX_MAX 20.0f
#define SPEED_STEP 0.1f

/* Decrease function */
static int on_dec_speed_max(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event)
{
    if (event.press) {
        k_mutex_lock(&variable_mutex, K_FOREVER);

        speed_max -= SPEED_STEP;
        if (speed_max < SPEED_MAX_MIN) {
            speed_max = SPEED_MAX_MIN;
        }

        LOG_INF("speed_max decreased to %f", (double)speed_max);

        k_mutex_unlock(&variable_mutex);
    }
    return ZMK_BEHAVIOR_OPAQUE;

}

/* Behavior driver API */
static const struct behavior_driver_api behavior_dec_api = {
    .binding_pressed = on_dec_speed_max,
};

/* Device registration */
DEVICE_DT_INST_DEFINE(DT_NODELABEL(dec_speed_max), NULL, NULL, NULL, NULL,
                      APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                      &behavior_dec_api);
