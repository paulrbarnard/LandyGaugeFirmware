/**
 * @file ble_tpms.h
 * @brief BLE TPMS (Tire Pressure Monitoring System) driver for AIYATO/ZEEPIN sensors
 * 
 * These sensors broadcast tire data in the BLE manufacturer data field.
 * Protocol format (18 bytes):
 *   Bytes 0-1:   Manufacturer ID (0001 = TomTom)
 *   Byte 2:      Sensor number (0x80=1, 0x81=2, 0x82=3, 0x83=4)
 *   Bytes 3-7:   Sensor address (EA:CA:XX:XX:XX)
 *   Bytes 8-11:  Pressure (little-endian, divide by 1000 for kPa, by 100000 for bar)
 *   Bytes 12-15: Temperature (little-endian, divide by 100 for °C)
 *   Byte 16:     Battery percentage
 *   Byte 17:     Alarm flag (0x00=OK, 0x01=No pressure)
 */

#ifndef BLE_TPMS_H
#define BLE_TPMS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of TPMS sensors we can track
#define TPMS_MAX_SENSORS 4

// Tire positions
typedef enum {
    TPMS_FRONT_LEFT = 0,
    TPMS_FRONT_RIGHT,
    TPMS_REAR_LEFT,
    TPMS_REAR_RIGHT,
    TPMS_POSITION_COUNT
} tpms_position_t;

// Alarm flags
typedef enum {
    TPMS_ALARM_NONE = 0x00,
    TPMS_ALARM_NO_PRESSURE = 0x01,
    TPMS_ALARM_HIGH_PRESSURE = 0x02,
    TPMS_ALARM_LOW_PRESSURE = 0x03,
    TPMS_ALARM_HIGH_TEMP = 0x04
} tpms_alarm_t;

// Data structure for a single tire sensor
typedef struct {
    uint8_t mac_address[6];     // Sensor MAC address
    float pressure_kpa;         // Pressure in kPa
    float pressure_bar;         // Pressure in bar
    float pressure_psi;         // Pressure in PSI
    float temperature_c;        // Temperature in Celsius
    uint8_t battery_percent;    // Battery level 0-100%
    tpms_alarm_t alarm;         // Alarm status
    int8_t rssi;                // Signal strength
    uint32_t last_update_ms;    // Timestamp of last update
    bool valid;                 // True if sensor has been seen
} tpms_sensor_data_t;

// Callback function type for TPMS updates
typedef void (*tpms_update_callback_t)(tpms_position_t position, const tpms_sensor_data_t *data);

/**
 * @brief Initialize the BLE TPMS subsystem
 * @return ESP_OK on success
 */
esp_err_t ble_tpms_init(void);

/**
 * @brief Start scanning for TPMS sensors
 * This runs continuous scanning in the background
 * @return ESP_OK on success
 */
esp_err_t ble_tpms_start_scan(void);

/**
 * @brief Stop scanning for TPMS sensors
 * @return ESP_OK on success
 */
esp_err_t ble_tpms_stop_scan(void);

/**
 * @brief Register a known sensor address for a tire position
 * @param position Tire position (FL, FR, RL, RR)
 * @param mac_address 6-byte MAC address of the sensor
 * @return ESP_OK on success
 */
esp_err_t ble_tpms_register_sensor(tpms_position_t position, const uint8_t *mac_address);

/**
 * @brief Register a sensor by MAC address string
 * @param position Tire position (FL, FR, RL, RR)
 * @param mac_str MAC address string like "80:EA:CA:10:8A:78"
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if parsing fails
 */
esp_err_t ble_tpms_register_sensor_str(tpms_position_t position, const char *mac_str);

/**
 * @brief Get current data for a specific tire
 * @param position Tire position
 * @param data Output structure for sensor data
 * @return ESP_OK if data available, ESP_ERR_NOT_FOUND if sensor not seen
 */
esp_err_t ble_tpms_get_data(tpms_position_t position, tpms_sensor_data_t *data);

/**
 * @brief Get all sensor data
 * @param data Array of TPMS_POSITION_COUNT sensor data structures
 */
void ble_tpms_get_all_data(tpms_sensor_data_t *data);

/**
 * @brief Register callback for TPMS updates
 * @param callback Function to call when sensor data is received
 */
void ble_tpms_register_callback(tpms_update_callback_t callback);

/**
 * @brief Check if any tire pressure is below threshold
 * @param threshold_bar Pressure threshold in bar
 * @return true if any tire is below threshold
 */
bool ble_tpms_any_low_pressure(float threshold_bar);

/**
 * @brief Check if a rapid pressure drop alarm is active
 * Triggers when pressure drops more than 5 PSI between consecutive readings
 * @return true if pressure drop alarm is active
 */
bool ble_tpms_check_pressure_drop_alarm(void);

/**
 * @brief Clear the pressure drop alarm after it has been handled
 */
void ble_tpms_clear_pressure_drop_alarm(void);

/**
 * @brief Get the tire position that triggered the pressure drop alarm
 * @return Tire position, or TPMS_POSITION_COUNT if no alarm
 */
tpms_position_t ble_tpms_get_pressure_drop_position(void);

/**
 * @brief Check if any TPMS sensor has reported data
 * @return true if at least one sensor has sent valid data
 */
bool ble_tpms_any_sensor_present(void);

/**
 * @brief Check if BLE TPMS is currently scanning
 * @return true if scanning active
 */
bool ble_tpms_is_scanning(void);

/**
 * @brief Periodic update - call from main loop to restart scanning when needed
 * Restarts scanning after the configured scan period has elapsed
 */
void ble_tpms_periodic_update(void);

/**
 * @brief Enable or disable fast scanning mode
 * Fast mode scans more frequently (every ~3s vs ~30s) for real-time updates
 * Useful when tire pressure gauge is actively displayed
 * @param enabled true for fast mode, false for normal mode
 */
void ble_tpms_set_fast_scan(bool enabled);

/**
 * @brief Check if fast scanning mode is enabled
 * @return true if fast mode active
 */
bool ble_tpms_is_fast_scan(void);

/**
 * @brief Temporarily pause BLE scanning (for SPI display updates)
 * Call before critical SPI operations to prevent radio interference
 */
void ble_tpms_pause_scan(void);

/**
 * @brief Resume BLE scanning after pause
 * Call after SPI operations complete
 */
void ble_tpms_resume_scan(void);

/**
 * @brief Get string representation of tire position
 * @param position Tire position
 * @return String like "FL", "FR", "RL", "RR"
 */
const char* ble_tpms_position_str(tpms_position_t position);

#ifdef __cplusplus
}
#endif

#endif // BLE_TPMS_H
