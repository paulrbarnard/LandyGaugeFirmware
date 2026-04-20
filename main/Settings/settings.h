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

/**
 * @brief Save tilt gauge zero-offset to NVS
 * @param offset_deg  The zero-offset angle in degrees
 */
void settings_save_tilt_offset(float offset_deg);

/**
 * @brief Save incline gauge zero-offset to NVS
 * @param offset_deg  The zero-offset angle in degrees
 */
void settings_save_incline_offset(float offset_deg);

/**
 * @brief Save incline gauge display mode to NVS
 * @param mode  0=degrees, 1=1-in-X, 2=% slope
 */
void settings_save_incline_mode(uint8_t mode);

/*******************************************************************************
 * WiFi credentials — stored in NVS, getters return "" if not configured
 ******************************************************************************/

void settings_save_wifi_home(const char *ssid, const char *pass);
void settings_save_wifi_phone(const char *ssid, const char *pass);

const char *settings_get_wifi_home_ssid(void);
const char *settings_get_wifi_home_pass(void);
const char *settings_get_wifi_phone_ssid(void);
const char *settings_get_wifi_phone_pass(void);

/*******************************************************************************
 * TPMS sensor MAC addresses — stored as 6-byte blobs in NVS
 ******************************************************************************/

#include "BLE_TPMS/ble_tpms.h"

/**
 * @brief Save a single TPMS sensor MAC to NVS
 */
void settings_save_tpms_mac(tpms_position_t pos, const uint8_t *mac);

/**
 * @brief Save all valid TPMS MACs to NVS in one commit
 */
void settings_save_all_tpms_macs(void);

/**
 * @brief Get a stored TPMS MAC address
 * @return true if MAC was found in NVS, false if not configured
 */
bool settings_get_tpms_mac(tpms_position_t pos, uint8_t *mac_out);

/*******************************************************************************
 * Timezone — POSIX TZ with automatic DST
 ******************************************************************************/

/** Apply the current timezone to the C library (setenv + tzset) */
void settings_apply_timezone(void);

/** Save timezone selection to NVS and apply immediately */
void settings_save_timezone(uint8_t idx);

/** @return current timezone table index */
uint8_t settings_get_timezone_index(void);

/** @return number of entries in the timezone table */
int settings_get_timezone_count(void);

/** @return friendly name for a timezone index */
const char *settings_get_timezone_name(int idx);

/** @return true if timezone has been explicitly configured */
bool settings_timezone_configured(void);

/*******************************************************************************
 * Solar twilight — automatic day/night when expansion board is absent
 ******************************************************************************/

/** @return representative latitude for the selected timezone */
float settings_get_latitude(void);

/**
 * @brief Check if it is dark (after civil dusk / before civil dawn)
 *
 * Uses the selected timezone's latitude and civil twilight (sun 6° below
 * horizon) to determine whether it is dark at the given local time.
 *
 * @return true if the current time is outside civil twilight daylight hours
 */
bool settings_is_dark(uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
