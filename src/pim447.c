/* pim447.c - Driver for Pimoroni PIM447 Trackball */

#define DT_DRV_COMPAT pimoroni_pim447

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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

LOG_MODULE_REGISTER(pim447, CONFIG_SENSOR_LOG_LEVEL);

/* Device configuration structure */
struct pim447_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
};

/* Device data structure */
struct pim447_data {
    const struct device *dev;
    struct k_work work;
    struct gpio_callback int_gpio_cb;
    bool sw_pressed_prev;
};

/* Forward declaration of functions */
static void pim447_work_handler(struct k_work *work);
static void pim447_gpio_callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins);
static int pim447_enable_interrupt(const struct pim447_config *config, bool enable);

static int pim447_read_motion_data(const struct device *dev, int16_t *delta_x, int16_t *delta_y, bool *sw_pressed) {
    const struct pim447_config *config = dev->config;
    uint8_t buf[5];
    int ret;

    // Read movement data and switch state
    ret = i2c_burst_read_dt(&config->i2c, REG_LEFT, buf, sizeof(buf));
    if (ret) {
        LOG_ERR("Failed to read motion data");
        return ret;
    }

    // Calculate movement deltas
    *delta_x = (int16_t)buf[1] - (int16_t)buf[0]; // RIGHT - LEFT
    *delta_y = (int16_t)buf[3] - (int16_t)buf[2]; // DOWN - UP
    *sw_pressed = (buf[4] & MSK_SWITCH_STATE) != 0;

    return 0;
}


static void pim447_report_motion(const struct device *dev, int16_t delta_x, int16_t delta_y, bool sw_pressed) {
    bool have_x = delta_x != 0;
    bool have_y = delta_y != 0;
    struct pim447_data *data = dev->data;

    if (have_x) {
        input_report(dev, EV_REL, REL_X, delta_x, false, K_NO_WAIT);
    }
    if (have_y) {
        input_report(dev, EV_REL, REL_Y, delta_y, false, K_NO_WAIT);
    }
    if (sw_pressed != data->sw_pressed_prev) {
        input_report(dev, EV_KEY, BTN_LEFT, sw_pressed ? 1 : 0, true, K_NO_WAIT);
        data->sw_pressed_prev = sw_pressed;
    }
}
static void pim447_work_handler(struct k_work *work) {
    struct pim447_data *data = CONTAINER_OF(work, struct pim447_data, work);
    const struct device *dev = data->dev;
    int16_t delta_x, delta_y;
    bool sw_pressed;
    int ret;

    ret = pim447_read_motion_data(dev, &delta_x, &delta_y, &sw_pressed);
    if (ret) {
        LOG_ERR("Failed to read motion data");
        return;
    }

    pim447_report_motion(dev, delta_x, delta_y, sw_pressed);
}


/* GPIO callback function */
static void pim447_gpio_callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins) {
    struct pim447_data *data = CONTAINER_OF(cb, struct pim447_data, int_gpio_cb);
    const struct pim447_config *config = data->dev->config;

    LOG_INF("GPIO callback triggered on pin %d", config->int_gpio.pin);

    k_work_submit(&data->work);
}

/* Function to enable or disable interrupt output */
static int pim447_enable_interrupt(const struct pim447_config *config, bool enable) {
    uint8_t int_reg;
    int ret;

    /* Read the current INT register value */
    ret = i2c_reg_read_byte_dt(&config->i2c, REG_INT, &int_reg);
    if (ret) {
        LOG_ERR("Failed to read INT register");
        return ret;
    }

    LOG_INF("INT register before enabling interrupt: 0x%02X", int_reg);

    /* Clear the MSK_INT_OUT_EN bit */
    int_reg &= ~MSK_INT_OUT_EN;

    /* Conditionally set the MSK_INT_OUT_EN bit */
    if (enable) {
        int_reg |= MSK_INT_OUT_EN;
    }

    /* Write the updated INT register value */
    ret = i2c_reg_write_byte_dt(&config->i2c, REG_INT, int_reg);
    if (ret) {
        LOG_ERR("Failed to write INT register");
        return ret;
    }

    LOG_INF("INT register after enabling interrupt: 0x%02X", int_reg);

    return 0;
}

/* Device initialization function */
static int pim447_init(const struct device *dev) {
    const struct pim447_config *config = dev->config;
    struct pim447_data *data = dev->data;
    int ret;

    data->dev = dev;
    data->sw_pressed_prev = false;

    /* Check if the I2C device is ready */
    if (!device_is_ready(config->i2c.bus)) {
        LOG_ERR("I2C bus device is not ready");
        return -ENODEV;
    }

    /* Read and log the chip ID */
    uint8_t chip_id_l, chip_id_h;
    ret = i2c_reg_read_byte_dt(&config->i2c, REG_CHIP_ID_L, &chip_id_l);
    if (ret) {
        LOG_ERR("Failed to read chip ID low byte");
        return ret;
    }

    ret = i2c_reg_read_byte_dt(&config->i2c, REG_CHIP_ID_H, &chip_id_h);
    if (ret) {
        LOG_ERR("Failed to read chip ID high byte");
        return ret;
    }

    uint16_t chip_id = ((uint16_t)chip_id_h << 8) | chip_id_l;
    LOG_INF("PIM447 chip ID: 0x%04X", chip_id);

    /* Check if the interrupt GPIO device is ready */
    if (!device_is_ready(config->int_gpio.port)) {
        LOG_ERR("Interrupt GPIO device is not ready");
        return -ENODEV;
    }

    /* Configure the interrupt GPIO pin without internal pull-up (external pull-up used) */
    ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
    if (ret) {
        LOG_ERR("Failed to configure interrupt GPIO");
        return ret;
    }

    /* Initialize the GPIO callback */
    gpio_init_callback(&data->int_gpio_cb, pim447_gpio_callback, BIT(config->int_gpio.pin));

    /* Add the GPIO callback */
    ret = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
    if (ret) {
        LOG_ERR("Failed to add GPIO callback");
        return ret;
    }

    /* Configure the GPIO interrupt for falling edge (active low) */
    ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_FALLING);
    if (ret) {
        LOG_ERR("Failed to configure GPIO interrupt");
        return ret;
    }

    /* Clear any pending interrupts */
    uint8_t int_status;
    ret = i2c_reg_read_byte_dt(&config->i2c, REG_INT, &int_status);
    if (ret) {
        LOG_ERR("Failed to read INT status register");
        return ret;
    }

    /* Clear the MSK_INT_TRIGGERED bit */
    int_status &= ~MSK_INT_TRIGGERED;
    ret = i2c_reg_write_byte_dt(&config->i2c, REG_INT, int_status);
    if (ret) {
        LOG_ERR("Failed to clear INT status register");
        return ret;
    }

    /* Enable interrupt output on the trackball */
    ret = pim447_enable_interrupt(config, true);
    if (ret) {
        LOG_ERR("Failed to enable interrupt output");
        return ret;
    }

    /* Initialize the work handler */
    k_work_init(&data->work, pim447_work_handler);

    LOG_INF("PIM447 driver initialized");

    return 0;
}

/* Device configuration */
static const struct pim447_config pim447_config = {
    .i2c = I2C_DT_SPEC_INST_GET(0),
    .int_gpio = GPIO_DT_SPEC_INST_GET(0, int_gpios),
};

/* Device data */
static struct pim447_data pim447_data;

/* Device initialization macro */
DEVICE_DT_INST_DEFINE(0, pim447_init, NULL, &pim447_data, &pim447_config,
                      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
