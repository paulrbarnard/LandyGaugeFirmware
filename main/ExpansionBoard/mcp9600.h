/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file mcp9600.h
 * @brief MCP9600 thermocouple EMF-to-temperature converter driver
 *
 * Supports K-type thermocouple with cold-junction compensation.
 * Provides hot-junction temperature reading and two configurable alerts.
 *
 * I2C address: 0x67 (default, ADDR pin floating/VDD).
 * Alert 1: Warning threshold (680°C default)
 * Alert 2: Danger threshold (750°C default)
 */

#ifndef MCP9600_H
#define MCP9600_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*******************************************************************************
 * I2C Configuration
 ******************************************************************************/
#define MCP9600_I2C_ADDR        0x60    /* ADDR pin pulled to GND via JP8 */

/*******************************************************************************
 * Register Addresses
 ******************************************************************************/
#define MCP9600_REG_TH          0x00    /* Hot-junction temperature (16-bit, 0.0625°C/LSB) */
#define MCP9600_REG_TD          0x01    /* Junction temperature delta Th-Tc */
#define MCP9600_REG_TC          0x02    /* Cold-junction temperature */
#define MCP9600_REG_RAW_ADC     0x03    /* Raw ADC data (24-bit) */
#define MCP9600_REG_STATUS      0x04    /* Status register */
#define MCP9600_REG_TC_CONFIG   0x05    /* Thermocouple sensor configuration */
#define MCP9600_REG_DEV_CONFIG  0x06    /* Device configuration */
#define MCP9600_REG_ALERT1_CFG  0x08    /* Alert 1 configuration */
#define MCP9600_REG_ALERT2_CFG  0x09    /* Alert 2 configuration */
#define MCP9600_REG_ALERT3_CFG  0x0A    /* Alert 3 configuration */
#define MCP9600_REG_ALERT4_CFG  0x0B    /* Alert 4 configuration */
#define MCP9600_REG_ALERT1_HYS  0x0C    /* Alert 1 hysteresis (°C) */
#define MCP9600_REG_ALERT2_HYS  0x0D    /* Alert 2 hysteresis */
#define MCP9600_REG_ALERT3_HYS  0x0E    /* Alert 3 hysteresis */
#define MCP9600_REG_ALERT4_HYS  0x0F    /* Alert 4 hysteresis */
#define MCP9600_REG_ALERT1_LIM  0x10    /* Alert 1 temperature limit (16-bit) */
#define MCP9600_REG_ALERT2_LIM  0x11    /* Alert 2 temperature limit */
#define MCP9600_REG_ALERT3_LIM  0x12    /* Alert 3 temperature limit */
#define MCP9600_REG_ALERT4_LIM  0x13    /* Alert 4 temperature limit */
#define MCP9600_REG_DEVICE_ID   0x20    /* Device ID / Revision */

/*******************************************************************************
 * Thermocouple Type (bits [6:4] of TC_CONFIG register)
 ******************************************************************************/
#define MCP9600_TC_TYPE_K       0x00    /* K-type (default) */
#define MCP9600_TC_TYPE_J       0x10
#define MCP9600_TC_TYPE_T       0x20
#define MCP9600_TC_TYPE_N       0x30
#define MCP9600_TC_TYPE_S       0x40
#define MCP9600_TC_TYPE_E       0x50
#define MCP9600_TC_TYPE_B       0x60
#define MCP9600_TC_TYPE_R       0x70

/*******************************************************************************
 * Status register bits
 ******************************************************************************/
#define MCP9600_STATUS_ALERT1   (1 << 0)    /* Alert 1 status */
#define MCP9600_STATUS_ALERT2   (1 << 1)    /* Alert 2 status */
#define MCP9600_STATUS_ALERT3   (1 << 2)    /* Alert 3 status */
#define MCP9600_STATUS_ALERT4   (1 << 3)    /* Alert 4 status */
#define MCP9600_STATUS_INPUT_RANGE (1 << 4) /* Input range exceeded */
#define MCP9600_STATUS_BURST    (1 << 5)    /* Burst complete */
#define MCP9600_STATUS_TH_UPD   (1 << 6)   /* Th update flag */
#define MCP9600_STATUS_SC_SHORT (1 << 7)    /* Short-circuit / open */

/*******************************************************************************
 * Alert configuration bits
 ******************************************************************************/
#define MCP9600_ALERT_ENABLE    (1 << 0)    /* Alert output enable */
#define MCP9600_ALERT_MODE_INT  (1 << 1)    /* 1=interrupt, 0=comparator */
#define MCP9600_ALERT_ACTIVE_HI (1 << 2)    /* 1=active-high, 0=active-low */
#define MCP9600_ALERT_TC_SELECT (1 << 3)    /* 1=cold junction, 0=hot junction */
#define MCP9600_ALERT_RISING    (1 << 4)    /* 1=rising (temp > limit), 0=falling */
#define MCP9600_ALERT_CLR_INT   (1 << 7)    /* Write 1 to clear interrupt flag */

/*******************************************************************************
 * EGT Alert Thresholds
 ******************************************************************************/
#define EGT_WARNING_TEMP_C      680.0f      /* Yellow warning threshold */
#define EGT_DANGER_TEMP_C       750.0f      /* Red danger threshold */

/*******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * @brief Initialise the MCP9600 thermocouple converter
 *
 * Configures K-type input, 18-bit ADC resolution, filter coefficient 4,
 * and sets up Alert 1 (680°C) and Alert 2 (750°C) in comparator mode.
 *
 * @return ESP_OK on success
 */
esp_err_t mcp9600_init(void);

/**
 * @brief Read the hot-junction (thermocouple) temperature
 * @param[out] temp_c  Temperature in degrees Celsius
 * @return ESP_OK on success
 */
esp_err_t mcp9600_read_temperature(float *temp_c);

/**
 * @brief Read the cold-junction (ambient) temperature
 * @param[out] temp_c  Temperature in degrees Celsius
 * @return ESP_OK on success
 */
esp_err_t mcp9600_read_cold_junction(float *temp_c);

/**
 * @brief Read the status register
 * @param[out] status  Raw status byte
 * @return ESP_OK on success
 */
esp_err_t mcp9600_read_status(uint8_t *status);

/**
 * @brief Check if Alert 1 (warning) is active
 * @return true if hot-junction temp >= 680°C
 */
bool mcp9600_alert1_active(void);

/**
 * @brief Check if Alert 2 (danger) is active
 * @return true if hot-junction temp >= 750°C
 */
bool mcp9600_alert2_active(void);

/**
 * @brief Clear alert interrupt flags (if using interrupt mode)
 */
void mcp9600_clear_alerts(void);

/**
 * @brief Check if MCP9600 was successfully initialised
 * @return true if device is present and initialised
 */
bool mcp9600_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP9600_H */
