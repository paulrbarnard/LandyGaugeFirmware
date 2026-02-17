/**
 * @file ads1115.c
 * @brief ADS1115 16-bit 4-channel I2C ADC driver implementation
 *
 * Uses single-shot mode by default: starts a conversion, waits for completion,
 * reads the result. This is power-efficient for periodic sampling.
 *
 * The ADS1115 uses 16-bit big-endian registers accessed over I2C.
 */

#include "ads1115.h"
#include "I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ADS1115";

// Current configuration
static ads1115_pga_t  current_pga = ADS1115_PGA_4096;   // ±4.096V default
static ads1115_dr_t   current_dr  = ADS1115_DR_128SPS;  // 128 SPS default

// Conversion timeout
#define ADS1115_CONVERSION_TIMEOUT_MS   200

/*******************************************************************************
 * Low-level register access
 * Note: ADS1115 uses 16-bit big-endian registers
 ******************************************************************************/

static esp_err_t ads1115_write_reg16(uint8_t reg, uint16_t value)
{
    uint8_t buf[2];
    buf[0] = (value >> 8) & 0xFF;   // MSB first
    buf[1] = value & 0xFF;          // LSB second
    esp_err_t ret = I2C_Write(ADS1115_I2C_ADDR, reg, buf, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t ads1115_read_reg16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2] = {0};
    esp_err_t ret = I2C_Read(ADS1115_I2C_ADDR, reg, buf, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    *value = ((uint16_t)buf[0] << 8) | buf[1];  // Big-endian
    return ESP_OK;
}

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

/**
 * @brief Get the voltage corresponding to 1 LSB for the current PGA setting
 */
static float get_lsb_voltage(void)
{
    // ADS1115 is 16-bit signed, so 15 bits for positive range
    switch (current_pga) {
        case ADS1115_PGA_6144: return 6.144f / 32768.0f;   // 187.5 µV/bit
        case ADS1115_PGA_4096: return 4.096f / 32768.0f;   // 125.0 µV/bit
        case ADS1115_PGA_2048: return 2.048f / 32768.0f;   //  62.5 µV/bit
        case ADS1115_PGA_1024: return 1.024f / 32768.0f;   //  31.25 µV/bit
        case ADS1115_PGA_0512: return 0.512f / 32768.0f;   //  15.625 µV/bit
        case ADS1115_PGA_0256: return 0.256f / 32768.0f;   //   7.8125 µV/bit
        default:               return 4.096f / 32768.0f;
    }
}

/**
 * @brief Get conversion time in ms for current data rate
 */
static uint32_t get_conversion_time_ms(void)
{
    switch (current_dr) {
        case ADS1115_DR_8SPS:   return 130;  // 1/8 = 125ms + margin
        case ADS1115_DR_16SPS:  return 65;
        case ADS1115_DR_32SPS:  return 35;
        case ADS1115_DR_64SPS:  return 20;
        case ADS1115_DR_128SPS: return 10;
        case ADS1115_DR_250SPS: return 6;
        case ADS1115_DR_475SPS: return 4;
        case ADS1115_DR_860SPS: return 3;
        default:                return 10;
    }
}

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

esp_err_t ads1115_init(void)
{
    // Read the config register to verify the device is present
    uint16_t config = 0;
    esp_err_t ret = ads1115_read_reg16(ADS1115_REG_CONFIG, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 not found at I2C address 0x%02X", ADS1115_I2C_ADDR);
        return ret;
    }

    // Default config register value for ADS1115 is 0x8583
    ESP_LOGI(TAG, "ADS1115 detected at 0x%02X (config=0x%04X)", ADS1115_I2C_ADDR, config);

    // Write default config: single-shot, ±4.096V, 128 SPS, comparator disabled
    uint16_t default_config = ADS1115_OS_START |
                              ADS1115_MUX_SINGLE_0 |
                              current_pga |
                              ADS1115_MODE_SINGLE |
                              current_dr |
                              ADS1115_COMP_QUE_DISABLE;
    ret = ads1115_write_reg16(ADS1115_REG_CONFIG, default_config);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "ADS1115 initialized: ±4.096V, 128 SPS, single-shot mode");
    return ESP_OK;
}

esp_err_t ads1115_read_raw(ads1115_mux_t mux, int16_t *raw)
{
    // Build config: start conversion, selected mux, current PGA/DR, single-shot
    uint16_t config = ADS1115_OS_START |
                      (uint16_t)mux |
                      current_pga |
                      ADS1115_MODE_SINGLE |
                      current_dr |
                      ADS1115_COMP_QUE_DISABLE;

    // Write config to start conversion
    esp_err_t ret = ads1115_write_reg16(ADS1115_REG_CONFIG, config);
    if (ret != ESP_OK) return ret;

    // Wait for conversion to complete
    uint32_t wait_ms = get_conversion_time_ms();
    vTaskDelay(pdMS_TO_TICKS(wait_ms));

    // Poll for conversion complete (OS bit = 1)
    uint32_t timeout = 0;
    uint16_t status = 0;
    do {
        ret = ads1115_read_reg16(ADS1115_REG_CONFIG, &status);
        if (ret != ESP_OK) return ret;
        if (status & ADS1115_OS_READY) break;
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout++;
    } while (timeout < ADS1115_CONVERSION_TIMEOUT_MS);

    if (!(status & ADS1115_OS_READY)) {
        ESP_LOGE(TAG, "Conversion timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Read conversion result
    uint16_t result = 0;
    ret = ads1115_read_reg16(ADS1115_REG_CONVERSION, &result);
    if (ret != ESP_OK) return ret;

    *raw = (int16_t)result;
    return ESP_OK;
}

esp_err_t ads1115_read_single(uint8_t channel, float *voltage)
{
    if (channel > 3) {
        ESP_LOGE(TAG, "Invalid channel %d (must be 0-3)", channel);
        return ESP_ERR_INVALID_ARG;
    }

    // Map channel number to mux setting
    ads1115_mux_t mux;
    switch (channel) {
        case 0: mux = ADS1115_MUX_SINGLE_0; break;
        case 1: mux = ADS1115_MUX_SINGLE_1; break;
        case 2: mux = ADS1115_MUX_SINGLE_2; break;
        case 3: mux = ADS1115_MUX_SINGLE_3; break;
        default: return ESP_ERR_INVALID_ARG;
    }

    int16_t raw = 0;
    esp_err_t ret = ads1115_read_raw(mux, &raw);
    if (ret != ESP_OK) return ret;

    *voltage = (float)raw * get_lsb_voltage();
    return ESP_OK;
}

esp_err_t ads1115_read_differential(ads1115_mux_t mux, float *voltage)
{
    int16_t raw = 0;
    esp_err_t ret = ads1115_read_raw(mux, &raw);
    if (ret != ESP_OK) return ret;

    *voltage = (float)raw * get_lsb_voltage();
    return ESP_OK;
}

void ads1115_set_gain(ads1115_pga_t pga)
{
    current_pga = pga;
    ESP_LOGI(TAG, "PGA set to ±%.3fV", ads1115_get_full_scale());
}

float ads1115_get_full_scale(void)
{
    switch (current_pga) {
        case ADS1115_PGA_6144: return 6.144f;
        case ADS1115_PGA_4096: return 4.096f;
        case ADS1115_PGA_2048: return 2.048f;
        case ADS1115_PGA_1024: return 1.024f;
        case ADS1115_PGA_0512: return 0.512f;
        case ADS1115_PGA_0256: return 0.256f;
        default:               return 4.096f;
    }
}

void ads1115_set_data_rate(ads1115_dr_t dr)
{
    current_dr = dr;
    ESP_LOGI(TAG, "Data rate changed");
}
