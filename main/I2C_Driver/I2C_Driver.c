/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

#include "I2C_Driver.h"

static const char *I2C_TAG = "I2C";

static i2c_master_bus_handle_t bus_handle = NULL;

/**
 * @brief Initialise the I2C master bus (new driver API).
 */
void I2C_Init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_MASTER_NUM,
        .scl_io_num = I2C_SCL_IO,
        .sda_io_num = I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
    ESP_LOGI(I2C_TAG, "I2C master bus initialised (new driver)");
}

i2c_master_bus_handle_t I2C_GetBusHandle(void)
{
    return bus_handle;
}

/*
 * Cached per-address device handles.
 * The new API requires a handle per device; we cache them lazily so that
 * callers (I2C_Read / I2C_Write) keep the same simple signature.
 */
#define MAX_I2C_DEVICES 16
typedef struct {
    uint8_t addr;
    i2c_master_dev_handle_t dev;
} dev_cache_entry_t;

static dev_cache_entry_t dev_cache[MAX_I2C_DEVICES];
static int dev_cache_count = 0;

/**
 * @brief Probe an I2C address.  Returns ESP_OK if the device ACKs.
 *        If a device handle exists (e.g. registered via I2C_AddDeviceFreq),
 *        probe using that handle's clock speed.  Otherwise fall back to
 *        a bus-level probe at the default bus speed.
 */
esp_err_t I2C_Probe(uint8_t addr, int timeout_ms)
{
    /* Check if we already have a device handle (with a custom clock rate) */
    for (int i = 0; i < dev_cache_count; i++) {
        if (dev_cache[i].addr == addr) {
            /* Use a write of just the address byte at the device's own SCL rate.
               i2c_master_transmit requires a valid buffer, so send 1 dummy byte. */
            uint8_t dummy = 0;
            return i2c_master_transmit(dev_cache[i].dev, &dummy, 1, timeout_ms);
        }
    }
    /* No cached handle — use bus-level probe (default speed) */
    return i2c_master_probe(bus_handle, addr, timeout_ms);
}

static i2c_master_dev_handle_t get_or_create_dev(uint8_t addr)
{
    /* Search cache */
    for (int i = 0; i < dev_cache_count; i++) {
        if (dev_cache[i].addr == addr) return dev_cache[i].dev;
    }
    /* Create new handle */
    if (dev_cache_count >= MAX_I2C_DEVICES) {
        ESP_LOGE(I2C_TAG, "I2C device cache full!");
        return NULL;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,  /* default 400 kHz */
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev);
    if (err != ESP_OK) {
        ESP_LOGE(I2C_TAG, "Failed to add device 0x%02X: %s", addr, esp_err_to_name(err));
        return NULL;
    }
    dev_cache[dev_cache_count].addr = addr;
    dev_cache[dev_cache_count].dev  = dev;
    dev_cache_count++;
    return dev;
}

/**
 * @brief Pre-register a device with a specific clock speed.
 *        Must be called BEFORE the first I2C_Read/I2C_Write to that address.
 */
esp_err_t I2C_AddDeviceFreq(uint8_t addr, uint32_t scl_speed_hz)
{
    /* Already cached? */
    for (int i = 0; i < dev_cache_count; i++) {
        if (dev_cache[i].addr == addr) return ESP_OK;
    }
    if (dev_cache_count >= MAX_I2C_DEVICES) {
        ESP_LOGE(I2C_TAG, "I2C device cache full!");
        return ESP_ERR_NO_MEM;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = scl_speed_hz,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev);
    if (err != ESP_OK) {
        ESP_LOGE(I2C_TAG, "Failed to add device 0x%02X @ %lu Hz: %s",
                 addr, (unsigned long)scl_speed_hz, esp_err_to_name(err));
        return err;
    }
    dev_cache[dev_cache_count].addr = addr;
    dev_cache[dev_cache_count].dev  = dev;
    dev_cache_count++;
    ESP_LOGI(I2C_TAG, "Added device 0x%02X @ %lu Hz", addr, (unsigned long)scl_speed_hz);
    return ESP_OK;
}

/**
 * @brief Write to a register (8-bit register address).
 */
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr,
                    const uint8_t *Reg_data, uint32_t Length)
{
    i2c_master_dev_handle_t dev = get_or_create_dev(Driver_addr);
    if (!dev) return ESP_ERR_INVALID_STATE;

    uint8_t buf[Length + 1];
    buf[0] = Reg_addr;
    memcpy(&buf[1], Reg_data, Length);

    return i2c_master_transmit(dev, buf, Length + 1, I2C_MASTER_TIMEOUT_MS);
}

/**
 * @brief Read from a register (8-bit register address).
 */
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr,
                   uint8_t *Reg_data, uint32_t Length)
{
    i2c_master_dev_handle_t dev = get_or_create_dev(Driver_addr);
    if (!dev) return ESP_ERR_INVALID_STATE;

    return i2c_master_transmit_receive(dev, &Reg_addr, 1,
                                       Reg_data, Length,
                                       I2C_MASTER_TIMEOUT_MS);
}
