/**
 * @file mcp9600.c
 * @brief MCP9600 thermocouple EMF-to-temperature converter driver
 *
 * K-type thermocouple with cold-junction compensation via I2C.
 * Alert 1 set to 680°C (EGT warning), Alert 2 set to 750°C (EGT danger).
 * Both alerts configured in comparator mode monitoring hot junction,
 * active-high, rising direction.
 */

#include "mcp9600.h"
#include "I2C_Driver.h"
#include "esp_log.h"

static const char *TAG = "MCP9600";

static bool mcp9600_ready = false;

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

/**
 * @brief Write a single byte to a register
 */
static esp_err_t mcp9600_write_reg8(uint8_t reg, uint8_t value)
{
    return I2C_Write(MCP9600_I2C_ADDR, reg, &value, 1);
}

/**
 * @brief Write a 16-bit value (big-endian) to a register
 */
static esp_err_t mcp9600_write_reg16(uint8_t reg, uint16_t value)
{
    uint8_t data[2] = {
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF)
    };
    return I2C_Write(MCP9600_I2C_ADDR, reg, data, 2);
}

/**
 * @brief Read 2 bytes (big-endian) from a register
 */
static esp_err_t mcp9600_read_reg16(uint8_t reg, uint8_t *buf)
{
    return I2C_Read(MCP9600_I2C_ADDR, reg, buf, 2);
}

/**
 * @brief Read 1 byte from a register
 */
static esp_err_t mcp9600_read_reg8(uint8_t reg, uint8_t *val)
{
    return I2C_Read(MCP9600_I2C_ADDR, reg, val, 1);
}

/**
 * @brief Convert 16-bit temperature register to float °C
 *
 * The MCP9600 hot-junction register is a signed 16-bit value.
 * Upper nibble of MSB is sign extension.
 * Resolution: 0.0625°C per LSB.
 */
static float temp_reg_to_celsius(uint8_t msb, uint8_t lsb)
{
    int16_t raw = (int16_t)((msb << 8) | lsb);
    return raw * 0.0625f;
}

/**
 * @brief Convert temperature in °C to 16-bit register value
 */
static uint16_t celsius_to_temp_reg(float temp_c)
{
    int16_t raw = (int16_t)(temp_c / 0.0625f);
    return (uint16_t)raw;
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

esp_err_t mcp9600_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing MCP9600 thermocouple converter...");

    /* Check Device ID — should read 0x40 for MCP9600 */
    uint8_t id_buf[2];
    err = mcp9600_read_reg16(MCP9600_REG_DEVICE_ID, id_buf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Device ID: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Device ID: 0x%02X, Revision: 0x%02X", id_buf[0], id_buf[1]);

    if (id_buf[0] != 0x40 && id_buf[0] != 0x41) {
        ESP_LOGW(TAG, "Unexpected device ID 0x%02X (expected 0x40/0x41)", id_buf[0]);
        /* Continue anyway — could be MCP9601 or similar */
    }

    /*
     * Thermocouple Sensor Configuration (0x05):
     *   [6:4] = 000 → K-type thermocouple
     *   [2:0] = 100 → Filter coefficient 4 (good noise filtering)
     */
    err = mcp9600_write_reg8(MCP9600_REG_TC_CONFIG, MCP9600_TC_TYPE_K | 0x04);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write TC config: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Device Configuration (0x06):
     *   [7]   = 0   → Cold-junction 0.0625°C resolution
     *   [6:5] = 00  → 18-bit ADC resolution (best accuracy)
     *   [4:2] = 000 → 1 burst sample
     *   [1:0] = 00  → Normal mode (continuous conversion)
     */
    err = mcp9600_write_reg8(MCP9600_REG_DEV_CONFIG, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write device config: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Alert 1: EGT Warning (680°C)
     *   Comparator mode, active-high, hot junction, rising direction, enabled
     */
    uint16_t alert1_limit = celsius_to_temp_reg(EGT_WARNING_TEMP_C);
    err = mcp9600_write_reg16(MCP9600_REG_ALERT1_LIM, alert1_limit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Alert 1 limit: %s", esp_err_to_name(err));
        return err;
    }
    err = mcp9600_write_reg8(MCP9600_REG_ALERT1_HYS, 10);  /* 10°C hysteresis */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Alert 1 hysteresis: %s", esp_err_to_name(err));
        return err;
    }
    err = mcp9600_write_reg8(MCP9600_REG_ALERT1_CFG,
                             MCP9600_ALERT_ENABLE |
                             MCP9600_ALERT_ACTIVE_HI |
                             MCP9600_ALERT_RISING);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Alert 1: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Alert 2: EGT Danger (750°C)
     *   Comparator mode, active-high, hot junction, rising direction, enabled
     */
    uint16_t alert2_limit = celsius_to_temp_reg(EGT_DANGER_TEMP_C);
    err = mcp9600_write_reg16(MCP9600_REG_ALERT2_LIM, alert2_limit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Alert 2 limit: %s", esp_err_to_name(err));
        return err;
    }
    err = mcp9600_write_reg8(MCP9600_REG_ALERT2_HYS, 10);  /* 10°C hysteresis */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Alert 2 hysteresis: %s", esp_err_to_name(err));
        return err;
    }
    err = mcp9600_write_reg8(MCP9600_REG_ALERT2_CFG,
                             MCP9600_ALERT_ENABLE |
                             MCP9600_ALERT_ACTIVE_HI |
                             MCP9600_ALERT_RISING);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Alert 2: %s", esp_err_to_name(err));
        return err;
    }

    mcp9600_ready = true;
    ESP_LOGI(TAG, "MCP9600 initialized: K-type, 18-bit, Alert1=%.0f°C, Alert2=%.0f°C",
             EGT_WARNING_TEMP_C, EGT_DANGER_TEMP_C);

    return ESP_OK;
}

esp_err_t mcp9600_read_temperature(float *temp_c)
{
    if (!mcp9600_ready || !temp_c) return ESP_ERR_INVALID_STATE;

    uint8_t buf[2];
    esp_err_t err = mcp9600_read_reg16(MCP9600_REG_TH, buf);
    if (err != ESP_OK) return err;

    *temp_c = temp_reg_to_celsius(buf[0], buf[1]);
    return ESP_OK;
}

esp_err_t mcp9600_read_cold_junction(float *temp_c)
{
    if (!mcp9600_ready || !temp_c) return ESP_ERR_INVALID_STATE;

    uint8_t buf[2];
    esp_err_t err = mcp9600_read_reg16(MCP9600_REG_TC, buf);
    if (err != ESP_OK) return err;

    *temp_c = temp_reg_to_celsius(buf[0], buf[1]);
    return ESP_OK;
}

esp_err_t mcp9600_read_status(uint8_t *status)
{
    if (!mcp9600_ready || !status) return ESP_ERR_INVALID_STATE;
    return mcp9600_read_reg8(MCP9600_REG_STATUS, status);
}

bool mcp9600_alert1_active(void)
{
    if (!mcp9600_ready) return false;
    uint8_t status = 0;
    if (mcp9600_read_reg8(MCP9600_REG_STATUS, &status) != ESP_OK) return false;
    return (status & MCP9600_STATUS_ALERT1) != 0;
}

bool mcp9600_alert2_active(void)
{
    if (!mcp9600_ready) return false;
    uint8_t status = 0;
    if (mcp9600_read_reg8(MCP9600_REG_STATUS, &status) != ESP_OK) return false;
    return (status & MCP9600_STATUS_ALERT2) != 0;
}

void mcp9600_clear_alerts(void)
{
    if (!mcp9600_ready) return;

    /* Read current alert configs and write back with CLR_INT bit set */
    uint8_t cfg;
    if (mcp9600_read_reg8(MCP9600_REG_ALERT1_CFG, &cfg) == ESP_OK) {
        mcp9600_write_reg8(MCP9600_REG_ALERT1_CFG, cfg | MCP9600_ALERT_CLR_INT);
    }
    if (mcp9600_read_reg8(MCP9600_REG_ALERT2_CFG, &cfg) == ESP_OK) {
        mcp9600_write_reg8(MCP9600_REG_ALERT2_CFG, cfg | MCP9600_ALERT_CLR_INT);
    }
}

bool mcp9600_is_ready(void)
{
    return mcp9600_ready;
}
