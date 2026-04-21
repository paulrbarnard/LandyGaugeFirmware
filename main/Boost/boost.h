/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file boost.h
 * @brief Boost gauge display using LVGL
 * 
 * Displays boost pressure 0-2.0 Bar with warning above 1.5 Bar.
 * Styled like the analog clock with day/night mode support.
 */

#ifndef BOOST_H
#define BOOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief Initialize the boost gauge display
 * Creates the gauge face with tick marks, numbers, and needle
 */
void boost_init(void);

/**
 * @brief Set the boost pressure value
 * @param psi Boost pressure in Bar (0-2.0 range, clamped if exceeded)
 */
void boost_set_value(float psi);

/**
 * @brief Get the current boost pressure value
 * @return Current boost pressure in PSI
 */
float boost_get_value(void);

/**
 * @brief Set boost gauge display mode (day/night)
 * @param is_night_mode true for night mode (green), false for day mode (white)
 */
void boost_set_night_mode(bool is_night_mode);

/**
 * @brief Show or hide the boost gauge
 * @param visible true to show, false to hide
 */
void boost_set_visible(bool visible);

/**
 * @brief Clean up boost gauge resources
 */
void boost_cleanup(void);

/**
 * @brief Set pressure units
 * @param use_bar true for Bar, false for PSI
 */
void boost_set_units_bar(bool use_bar);

/**
 * @brief Toggle pressure units between PSI and Bar
 */
void boost_toggle_units(void);

#ifdef __cplusplus
}
#endif

#endif // BOOST_H
