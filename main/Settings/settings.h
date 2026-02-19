/**
 * @file settings.h
 * @brief Persistent settings via NVS (non-volatile storage)
 *
 * Stores user preferences and calibration data so they survive reboots:
 *   - Compass calibration (hard-iron offsets + soft-iron scales)
 *   - Boost gauge units (BAR / PSI)
 *   - Tire pressure gauge units (BAR / PSI)
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load all persisted settings and apply them
 *
 * Call once at startup after NVS flash and peripheral drivers are initialised.
 * Missing keys are silently ignored (first-boot defaults remain).
 *
 * @return ESP_OK on success
 */
esp_err_t settings_load(void);

/**
 * @brief Save compass calibration to NVS
 *
 * Reads the current calibration from lis3mdl and writes offsets + scales.
 */
void settings_save_compass_cal(void);

/**
 * @brief Save boost gauge unit preference to NVS
 * @param use_bar  true = BAR, false = PSI
 */
void settings_save_boost_units(bool use_bar);

/**
 * @brief Save tire-pressure gauge unit preference to NVS
 * @param use_bar  true = BAR, false = PSI
 */
void settings_save_tpms_units(bool use_bar);

/**
 * @brief Save tire-pressure gauge display mode to NVS
 * @param mode  0=BAR°C, 1=PSI°C, 2=BAR°F, 3=PSI°F
 */
void settings_save_tpms_mode(uint8_t mode);

/**
 * @brief Save EGT gauge unit preference to NVS
 * @param use_celsius  true = °C, false = °F
 */
void settings_save_egt_units(bool use_celsius);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
