/**
 * @file button_input.h
 * @brief Debounced GPIO button input driver for gauge switching
 * 
 * This driver provides debounced button input on GPIO0 (active low).
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
 * Sets up GPIO0 as input with internal pull-up and configures
 * a debounce timer for reliable button press detection.
 * 
 * @return ESP_OK on success
 */
esp_err_t button_input_init(void);

/**
 * @brief Check if a button press has been detected (debounced)
 * 
 * This function returns true once per button press after debounce.
 * It clears the pending press flag after returning true.
 * 
 * @return true if a debounced button press was detected
 */
bool button_input_pressed(void);

/**
 * @brief Clean up button input driver
 */
void button_input_cleanup(void);

#ifdef __cplusplus
}
#endif
