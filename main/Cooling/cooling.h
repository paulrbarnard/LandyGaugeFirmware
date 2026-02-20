/**
 * @file cooling.h
 * @brief Cooling status gauge display using LVGL
 *
 * Displays three sectors showing cooling system status:
 *   - Top-left:  Fan Low (IN3)  — rotating fan icon when active
 *   - Top-right: Fan High (IN4) — rotating fan icon when active
 *   - Bottom:    Coolant level (IN5) — radiator icon with water level
 *
 * Wading mode: select button toggles OUT1 on expansion board.
 * When active both fan symbols turn red. Plays wadeOn.mp3 / wadeOff.mp3.
 *
 * Auto-switches to this gauge when FanLow, FanHigh, or Coolant Low activates.
 */

#ifndef COOLING_H
#define COOLING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief Initialize the cooling gauge display
 * Creates the three-sector layout with fan and radiator icons
 */
void cooling_init(void);

/**
 * @brief Update the cooling gauge state from expansion board inputs
 * Call periodically from the main loop (~10 Hz)
 */
void cooling_update(void);

/**
 * @brief Set day/night display mode
 * @param night true for night mode (green accent), false for day mode (white accent)
 */
void cooling_set_night_mode(bool night);

/**
 * @brief Show or hide the cooling gauge
 * @param visible true to show, false to hide
 */
void cooling_set_visible(bool visible);

/**
 * @brief Clean up all LVGL objects and timers
 */
void cooling_cleanup(void);

/**
 * @brief Toggle wading mode on/off
 * Activates/deactivates OUT1 on expansion board, plays MP3 notification
 */
void cooling_toggle_wading(void);

/**
 * @brief Check if any cooling alarm is active
 * @return true if FanLow, FanHigh, or Coolant Low is active
 */
bool cooling_alarm_active(void);

/**
 * @brief Get wading mode state
 * @return true if wading mode is currently active
 */
bool cooling_get_wading(void);

#ifdef __cplusplus
}
#endif

#endif // COOLING_H
