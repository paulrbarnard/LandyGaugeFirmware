#ifndef INCLINE_H
#define INCLINE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the incline (pitch) gauge display
 */
void incline_init(void);

/**
 * @brief Show or hide the incline gauge
 * @param visible true to show, false to hide
 */
void incline_set_visible(bool visible);

/**
 * @brief Set the incline angle (rotates the vertical plumb line)
 * @param angle_degrees Pitch in degrees (positive = nose up, negative = nose down)
 */
void incline_set_angle(float angle_degrees);

/**
 * @brief Set the display mode (day/night)
 * @param night true for night mode (dark image), false for day mode
 */
void incline_set_night_mode(bool night);

/**
 * @brief Zero the incline gauge at the current angle
 *
 * Captures the current IMU pitch as the zero-offset and saves it to NVS.
 */
void incline_zero_offset(void);

/**
 * @brief Set the incline zero-offset directly (used when restoring from NVS)
 * @param offset_deg  The offset angle in degrees
 */
void incline_set_offset(float offset_deg);

/**
 * @brief Cycle the display-value mode: degrees -> 1-in-X -> % slope -> degrees
 */
void incline_cycle_mode(void);

/**
 * @brief Set the display mode directly (used when restoring from NVS)
 * @param mode 0=degrees, 1=1-in-X, 2=% slope
 */
void incline_set_mode(uint8_t mode);

/**
 * @brief Clean up incline gauge resources
 */
void incline_cleanup(void);

#endif // INCLINE_H
