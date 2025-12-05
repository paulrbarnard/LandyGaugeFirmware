/**
 * @file clock.h
 * @brief Analog clock display using LVGL
 */

#ifndef CLOCK_H
#define CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>

/**
 * @brief Initialize the analog clock display
 * Creates the clock face with hour markers and numbers
 */
void clock_init(void);

/**
 * @brief Update the clock hands to show the current time
 * @param hour Hour (0-23)
 * @param minute Minute (0-59)
 * @param second Second (0-59)
 */
void clock_update(uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief Set clock display mode (day/night)
 * @param is_night_mode true for night mode (green), false for day mode (white)
 */
void clock_set_night_mode(bool is_night_mode);

/**
 * @brief Show or hide the clock
 * @param visible true to show, false to hide
 */
void clock_set_visible(bool visible);

/**
 * @brief Clean up clock resources
 */
void clock_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_H */
