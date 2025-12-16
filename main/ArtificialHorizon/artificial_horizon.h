/**
 * @file artificial_horizon.h
 * @brief Artificial horizon (attitude indicator) gauge using IMU data
 */

#ifndef ARTIFICIAL_HORIZON_H
#define ARTIFICIAL_HORIZON_H

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief Initialize the artificial horizon display
 * 
 * Creates the artificial horizon gauge on the screen using LVGL.
 * The horizon uses pitch and roll data from the IMU to show aircraft attitude.
 */
void artificial_horizon_init(void);

/**
 * @brief Update the artificial horizon with current IMU data
 * 
 * @param pitch Pitch angle in degrees (-90 to +90, positive = nose up)
 * @param roll Roll angle in degrees (-180 to +180, positive = right wing down)
 */
void artificial_horizon_update(float pitch, float roll);

/**
 * @brief Set day/night mode for the artificial horizon
 * 
 * @param night_mode true for night mode (green), false for day mode (white)
 */
void artificial_horizon_set_night_mode(bool night_mode);

/**
 * @brief Show or hide the artificial horizon
 * 
 * @param visible true to show, false to hide
 */
void artificial_horizon_set_visible(bool visible);

/**
 * @brief Clean up and destroy the artificial horizon
 * 
 * Frees all LVGL objects and resets state.
 */
void artificial_horizon_cleanup(void);

#endif // ARTIFICIAL_HORIZON_H
