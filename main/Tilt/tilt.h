#ifndef TILT_H
#define TILT_H

#include <stdbool.h>

/**
 * @brief Initialize the tilt gauge display
 */
void tilt_init(void);

/**
 * @brief Show or hide the tilt gauge
 * @param visible true to show, false to hide
 */
void tilt_set_visible(bool visible);

/**
 * @brief Set the tilt angle (rotates the rear-view vehicle image)
 * @param angle_degrees The tilt angle in degrees (positive = tilt right, negative = tilt left)
 */
void tilt_set_angle(float angle_degrees);

/**
 * @brief Set the display mode (day/night)
 * @param night true for night mode (dark image), false for day mode (light image)
 */
void tilt_set_night_mode(bool night);

#endif // TILT_H
