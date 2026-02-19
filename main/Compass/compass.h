/**
 * @file compass.h
 * @brief Compass gauge display using LVGL and LIS3MDL magnetometer
 *
 * Displays a compass rose with heading from the LIS3MDL magnetometer
 * on the expansion board. Shows cardinal/intercardinal directions
 * with a rotating compass card and fixed heading indicator.
 * Day/night mode support matching other gauges.
 */

#ifndef COMPASS_H
#define COMPASS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief Initialize the compass gauge display
 * Creates the compass rose with cardinal markers and heading indicator
 */
void compass_init(void);

/**
 * @brief Update the compass with a new heading
 * @param heading Compass heading in degrees (0=N, 90=E, 180=S, 270=W)
 */
void compass_set_heading(float heading);

/**
 * @brief Get the current compass heading
 * @return Current heading in degrees (0-360)
 */
float compass_get_heading(void);

/**
 * @brief Set compass gauge display mode (day/night)
 * @param is_night_mode true for night mode (green), false for day mode (white)
 */
void compass_set_night_mode(bool is_night_mode);

/**
 * @brief Show or hide the compass gauge
 * @param visible true to show, false to hide
 */
void compass_set_visible(bool visible);

/**
 * @brief Clean up compass gauge resources
 */
void compass_cleanup(void);

/**
 * @brief Toggle magnetometer calibration mode
 *
 * First call starts calibration (shows "CALIBRATING" overlay).
 * Second call stops calibration, applies offsets, returns to normal.
 */
void compass_toggle_calibration(void);

/**
 * @brief Check if compass is currently calibrating
 * @return true if calibration in progress
 */
bool compass_is_calibrating(void);

#ifdef __cplusplus
}
#endif

#endif // COMPASS_H
