/**
 * @file tire_pressure.h
 * @brief Tire Pressure gauge display using LVGL
 */

#ifndef TIRE_PRESSURE_H
#define TIRE_PRESSURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the tire pressure gauge display
 * Creates the gauge face with the roof image centered
 */
void tire_pressure_init(void);

/**
 * @brief Set tire pressure display mode (day/night)
 * @param is_night_mode true for night mode (green), false for day mode (white)
 */
void tire_pressure_set_night_mode(bool is_night_mode);

/**
 * @brief Show or hide the tire pressure gauge
 * @param visible true to show, false to hide
 */
void tire_pressure_set_visible(bool visible);

/**
 * @brief Set the pressure value for a specific wheel
 * @param wheel 0=front-left, 1=front-right, 2=rear-left, 3=rear-right
 * @param pressure_psi Pressure value in PSI (will be converted if units are bar)
 */
void tire_pressure_set_value(int wheel, float pressure_psi);

/**
 * @brief Set all sensor data for a specific wheel (pressure, temperature, battery)
 * @param wheel 0=front-left, 1=front-right, 2=rear-left, 3=rear-right
 * @param pressure_psi Pressure value in PSI
 * @param temp_c Temperature value in Celsius
 * @param battery_pct Battery percentage (0-100)
 */
void tire_pressure_set_sensor_data(int wheel, float pressure_psi, float temp_c, uint8_t battery_pct);

/**
 * @brief Set all four tire pressure values at once
 * @param fl Front-left pressure in PSI
 * @param fr Front-right pressure in PSI
 * @param rl Rear-left pressure in PSI
 * @param rr Rear-right pressure in PSI
 */
void tire_pressure_set_all_values(float fl, float fr, float rl, float rr);

/**
 * @brief Toggle pressure units between PSI and Bar
 */
void tire_pressure_toggle_units(void);

/**
 * @brief Set pressure units
 * @param use_bar true for Bar, false for PSI
 */
void tire_pressure_set_units_bar(bool use_bar);

/**
 * @brief Set TPMS display mode (pressure + temperature units)
 * @param mode 0=BAR°C, 1=PSI°C, 2=BAR°F, 3=PSI°F
 */
void tire_pressure_set_mode(uint8_t mode);

/**
 * @brief Clean up tire pressure gauge resources
 */
void tire_pressure_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // TIRE_PRESSURE_H
