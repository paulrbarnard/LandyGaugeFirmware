/**
 * @file lis3mdl.c
 * @brief LIS3MDL 3-axis magnetometer driver implementation
 *
 * ST LIS3MDL: ±4/8/12/16 gauss, 16-bit, I2C digital magnetic sensor.
 * Configured for continuous mode with high-performance XY and Z operation.
 */

#include "lis3mdl.h"
#include "I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "LIS3MDL";

// Calibration offsets
static lis3mdl_calibration_t cal_offsets = { .x_offset = 0, .y_offset = 0, .z_offset = 0 };

// Current full-scale setting
static lis3mdl_fs_t current_fs = LIS3MDL_FS_4G;

// Sensitivity in LSB/gauss for each full-scale setting
// From datasheet: ±4G → 6842 LSB/G, ±8G → 3421, ±12G → 2281, ±16G → 1711
#define SENSITIVITY_4G   6842.0f
#define SENSITIVITY_8G   3421.0f
#define SENSITIVITY_12G  2281.0f
#define SENSITIVITY_16G  1711.0f

// 1 Gauss = 100 µT
#define GAUSS_TO_UT      100.0f

/*******************************************************************************
 * Low-level register access
 ******************************************************************************/

static esp_err_t lis3mdl_write_reg(uint8_t reg, uint8_t data)
{
    esp_err_t ret = I2C_Write(LIS3MDL_I2C_ADDR, reg, &data, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t lis3mdl_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t ret = I2C_Read(LIS3MDL_I2C_ADDR, reg, data, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t lis3mdl_read_regs(uint8_t reg, uint8_t *data, uint32_t len)
{
    // LIS3MDL auto-increments when MSB of sub-address is set
    esp_err_t ret = I2C_Read(LIS3MDL_I2C_ADDR, reg | 0x80, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read regs 0x%02X (%lu bytes) failed: %s", reg, (unsigned long)len, esp_err_to_name(ret));
    }
    return ret;
}

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

static float get_sensitivity(void)
{
    switch (current_fs) {
        case LIS3MDL_FS_4G:  return SENSITIVITY_4G;
        case LIS3MDL_FS_8G:  return SENSITIVITY_8G;
        case LIS3MDL_FS_12G: return SENSITIVITY_12G;
        case LIS3MDL_FS_16G: return SENSITIVITY_16G;
        default:             return SENSITIVITY_4G;
    }
}

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

esp_err_t lis3mdl_init(void)
{
    esp_err_t ret;

    // Read WHO_AM_I to verify chip presence
    uint8_t who_am_i = 0;
    ret = lis3mdl_read_reg(LIS3MDL_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LIS3MDL not found at I2C address 0x%02X", LIS3MDL_I2C_ADDR);
        return ret;
    }

    if (who_am_i != LIS3MDL_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X (expected 0x%02X)", who_am_i, LIS3MDL_WHO_AM_I_VALUE);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "LIS3MDL detected at 0x%02X (WHO_AM_I=0x%02X)", LIS3MDL_I2C_ADDR, who_am_i);

    // Soft reset
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG2, LIS3MDL_SOFT_RST);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    // CTRL_REG1: Temperature enabled, high-performance XY, 10 Hz ODR
    uint8_t ctrl1 = LIS3MDL_TEMP_EN | LIS3MDL_OM_HIGH | LIS3MDL_ODR_10HZ;
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG1, ctrl1);
    if (ret != ESP_OK) return ret;

    // CTRL_REG2: ±4 gauss full-scale
    current_fs = LIS3MDL_FS_4G;
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG2, (uint8_t)current_fs);
    if (ret != ESP_OK) return ret;

    // CTRL_REG3: Continuous conversion mode
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG3, (uint8_t)LIS3MDL_MD_CONTINUOUS);
    if (ret != ESP_OK) return ret;

    // CTRL_REG4: High-performance mode for Z axis
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG4, 0x08);  // OMZ = high performance
    if (ret != ESP_OK) return ret;

    // CTRL_REG5: Block data update enabled
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG5, 0x40);  // BDU = 1
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "LIS3MDL initialized: continuous mode, 10Hz, ±4G, high-performance");
    return ESP_OK;
}

bool lis3mdl_data_ready(void)
{
    uint8_t status = 0;
    if (lis3mdl_read_reg(LIS3MDL_REG_STATUS, &status) != ESP_OK) {
        return false;
    }
    return (status & LIS3MDL_STATUS_ZYXDA) != 0;
}

esp_err_t lis3mdl_read_raw(lis3mdl_raw_t *raw)
{
    // Read all 6 data bytes in one burst (auto-increment)
    uint8_t buf[6] = {0};
    esp_err_t ret = lis3mdl_read_regs(LIS3MDL_REG_OUT_X_L, buf, 6);
    if (ret != ESP_OK) return ret;

    // Data is little-endian (LSB first)
    raw->x = (int16_t)((buf[1] << 8) | buf[0]);
    raw->y = (int16_t)((buf[3] << 8) | buf[2]);
    raw->z = (int16_t)((buf[5] << 8) | buf[4]);

    return ESP_OK;
}

esp_err_t lis3mdl_read_data(lis3mdl_data_t *data)
{
    lis3mdl_raw_t raw;
    esp_err_t ret = lis3mdl_read_raw(&raw);
    if (ret != ESP_OK) return ret;

    float sensitivity = get_sensitivity();
    data->x = ((float)(raw.x - cal_offsets.x_offset) / sensitivity) * GAUSS_TO_UT;
    data->y = ((float)(raw.y - cal_offsets.y_offset) / sensitivity) * GAUSS_TO_UT;
    data->z = ((float)(raw.z - cal_offsets.z_offset) / sensitivity) * GAUSS_TO_UT;

    return ESP_OK;
}

esp_err_t lis3mdl_get_heading(float *heading)
{
    lis3mdl_raw_t raw;
    esp_err_t ret = lis3mdl_read_raw(&raw);
    if (ret != ESP_OK) return ret;

    float x = (float)(raw.x - cal_offsets.x_offset);
    float y = (float)(raw.y - cal_offsets.y_offset);

    float heading_rad = atan2f(y, x);
    float heading_deg = heading_rad * (180.0f / M_PI);
    if (heading_deg < 0.0f) {
        heading_deg += 360.0f;
    }

    *heading = heading_deg;
    return ESP_OK;
}

void lis3mdl_set_calibration(const lis3mdl_calibration_t *cal)
{
    cal_offsets = *cal;
    ESP_LOGI(TAG, "Calibration set: x=%d, y=%d, z=%d",
             cal->x_offset, cal->y_offset, cal->z_offset);
}

esp_err_t lis3mdl_read_temperature(float *temp_c)
{
    uint8_t buf[2] = {0};
    esp_err_t ret = lis3mdl_read_regs(LIS3MDL_REG_TEMP_OUT_L, buf, 2);
    if (ret != ESP_OK) return ret;

    int16_t raw_temp = (int16_t)((buf[1] << 8) | buf[0]);
    // LIS3MDL: 8 LSB/°C, with 25°C = 0 offset
    *temp_c = ((float)raw_temp / 8.0f) + 25.0f;

    return ESP_OK;
}
