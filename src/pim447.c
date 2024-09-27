#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>

LOG_MODULE_REGISTER(pim447, CONFIG_INPUT_LOG_LEVEL);

/* Register Addresses */
#define REG_LEFT    0x04
#define REG_RIGHT   0x05
#define REG_UP      0x06
#define REG_DOWN    0x07
#define REG_INT     0xF9

/* Interrupt Masks */
#define MSK_INT_TRIGGERED   0b00000001
#define MSK_INT_OUT_EN      0b00000010

#define DIRECTION_COUNT 4

/* Device configuration structure */
struct pim447_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
};

/* Direction data structure */
struct direction_data {
    const struct device *dev;
    uint8_t reg;
    int8_t value;
    uint16_t input_code;
};

/* Device data structure */
struct pim447_data {
    const struct device *dev;
    struct k_work direction_works[DIRECTION_COUNT];
    struct direction_data direction_data[DIRECTION_COUNT];
    struct gpio_callback int_gpio_cb;
};

static void process_direction(struct k_work *work)
{
    struct direction_data *data = CONTAINER_OF(work, struct direction_data, work);
    const struct pim447_config *config = data->dev->config;
    uint8_t value;
    int ret;

    /* Read direction value */
    ret = i2c_reg_read_byte_dt(&config->i2c, data->reg, &value);
    if (ret) {
        LOG_ERR("Failed to read direction data from PIM447");
        return;
    }

    /* Report movement if non-zero */
    if (value != 0) {
        struct input_event event;
        event.type = INPUT_EV_REL;
        event.code = data->input_code;
        event.value = (int16_t)((int8_t)value);
        input_report(data->dev, &event, 1, K_NO_WAIT);
        LOG_DBG("Reported movement: dir=%d, value=%d", data->input_code, (int8_t)value);
    }

    /* Clear direction register */
    i2c_reg_write_byte_dt(&config->i2c, data->reg, 0);
}

static void pim447_gpio_callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
    struct pim447_data *data = CONTAINER_OF(cb, struct pim447_data, int_gpio_cb);
    const struct pim447_config *config = data->dev->config;

    /* Submit work for each direction */
    for (int i = 0; i < DIRECTION_COUNT; i++) {
        k_work_submit(&data->direction_works[i]);
    }

    /* Clear the INT status register */
    uint8_t int_status;
    i2c_reg_read_byte_dt(&config->i2c, REG_INT, &int_status);
    int_status &= ~MSK_INT_TRIGGERED;
    i2c_reg_write_byte_dt(&config->i2c, REG_INT, int_status);
}

static int pim447_init(const struct device *dev)
{
    const struct pim447_config *config = dev->config;
    struct pim447_data *data = dev->data;

    if (!device_is_ready(config->i2c.bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    data->dev = dev;

    /* Initialize direction-specific work items and data */
    static const uint8_t direction_regs[DIRECTION_COUNT] = {REG_LEFT, REG_RIGHT, REG_UP, REG_DOWN};
    static const uint16_t input_codes[DIRECTION_COUNT] = {INPUT_REL_X, INPUT_REL_X, INPUT_REL_Y, INPUT_REL_Y};
    static const int8_t direction_multipliers[DIRECTION_COUNT] = {-1, 1, -1, 1};

    for (int i = 0; i < DIRECTION_COUNT; i++) {
        k_work_init(&data->direction_works[i], process_direction);
        data->direction_data[i].dev = dev;
        data->direction_data[i].reg = direction_regs[i];
        data->direction_data[i].input_code = input_codes[i];
        data->direction_data[i].value *= direction_multipliers[i];
    }

    /* Configure interrupt GPIO */
    gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_FALLING);

    /* Initialize GPIO callback */
    gpio_init_callback(&data->int_gpio_cb, pim447_gpio_callback, BIT(config->int_gpio.pin));
    gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);

    /* Enable interrupt output on the trackball */
    uint8_t int_reg;
    i2c_reg_read_byte_dt(&config->i2c, REG_INT, &int_reg);
    int_reg |= MSK_INT_OUT_EN;
    i2c_reg_write_byte_dt(&config->i2c, REG_INT, int_reg);

    LOG_INF("PIM447 driver initialized");

    return 0;
}

static const struct input_interface_api pim447_input_api = {
    /* The PIM447 doesn't require any specific input API functions */
};

/* Device configuration */
static const struct pim447_config pim447_config = {
    .i2c = I2C_DT_SPEC_INST_GET(0),
    .int_gpio = GPIO_DT_SPEC_INST_GET(0, int_gpios),
};

/* Device data */
static struct pim447_data pim447_data;

/* Device initialization macro */
DEVICE_DT_INST_DEFINE(0, pim447_init, NULL, &pim447_data, &pim447_config,
                      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, &pim447_input_api);