/**
 * @file egt.h
 * @brief Exhaust Gas Temperature gauge display using LVGL
 *
 * Displays EGT from MCP9600 thermocouple converter.
 * Range: 0–900°C / 0–1650°F
 * Warning (yellow) above 680°C / 1256°F
 * Danger (red) above 750°C / 1382°F
 * Styled to match the boost gauge with day/night mode support.
 */

#ifndef EGT_H
#define EGT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief Initialize the EGT gauge display
 */
void egt_init(void);

/**
 * @brief Set the EGT value in degrees Celsius
 * @param temp_c  Temperature in °C (0–900 range, clamped if exceeded)
 */
void egt_set_value(float temp_c);

/**
 * @brief Get the current EGT value in °C
 * @return Current EGT in degrees Celsius
 */
float egt_get_value(void);

/**
 * @brief Set EGT gauge display mode (day/night)
 * @param is_night_mode  true for night mode, false for day mode
 */
void egt_set_night_mode(bool is_night_mode);

/**
 * @brief Show or hide the EGT gauge
 * @param visible  true to show, false to hide
 */
void egt_set_visible(bool visible);

/**
 * @brief Clean up EGT gauge resources
 */
void egt_cleanup(void);

/**
 * @brief Set temperature units
 * @param use_celsius  true for °C, false for °F
 */
void egt_set_units_celsius(bool use_celsius);

/**
 * @brief Toggle temperature units between °C and °F
 */
void egt_toggle_units(void);

/**
 * @brief Check if display is in Celsius mode
 * @return true if Celsius, false if Fahrenheit
 */
bool egt_is_celsius(void);

#ifdef __cplusplus
}
#endif

#endif /* EGT_H */
