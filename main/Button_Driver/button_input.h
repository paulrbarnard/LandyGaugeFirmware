/**
 * @file button_input.h
 * @brief Debounced GPIO button input driver for gauge switching
 * 
 * Supports three buttons:
 *   GPIO0  - Boot button (active low) - legacy next gauge
 *   GPIO43 - Next gauge button (active low, pull-up)
 *   GPIO44 - Previous gauge button (active low, pull-up)
 *
 * Works on both touch and non-touch versions of the hardware.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the button input driver
 * 
 * Sets up GPIO0, GPIO43, and GPIO44 as inputs with internal pull-ups
 * and configures software debounce for reliable press detection.
 * 
 * @return ESP_OK on success
 */
esp_err_t button_input_init(void);

/**
 * @brief Check if a "next" button press was detected (debounced)
 * 
 * Returns true once per press on GPIO0 (boot) or GPIO43 (next).
 * Clears the pending flag after returning true.
 * 
 * @return true if a debounced next-button press was detected
 */
bool button_input_pressed(void);

/**
 * @brief Check if the "previous" button press was detected (debounced)
 * 
 * Returns true once per press on GPIO44 (prev).
 * Clears the pending flag after returning true.
 * 
 * @return true if a debounced prev-button press was detected
 */
bool button_input_prev_pressed(void);

/**
 * @brief Clean up button input driver
 */
void button_input_cleanup(void);

#ifdef __cplusplus
}
#endif
