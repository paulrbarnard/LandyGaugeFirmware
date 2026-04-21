/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file expansion_board.h
 * @brief Expansion board manager — digital inputs, ADC, magnetometer
 *
 * High-level module that ties together the MCP23017 I/O expander,
 * ADS1115 ADC, and QMC5883L magnetometer on the expansion board.
 *
 * Digital inputs (Port B, active high in software after polarity config):
 *   IO0 - Reserved          (future expansion)
 *   IO1 - Ignition on      (active-high opto, IPOL inverted)
 *   IO2 - Lights on        (active-high opto, IPOL inverted)
 *   IO3 - Fan low speed    (active-low thermo switch, IPOL inverted + SW flip)
 *   IO4 - Fan high speed   (active-low thermo switch, IPOL inverted + SW flip)
 *   IO5 - Coolant low      (active-high opto, IPOL inverted)
 *   IO6 - Low beam (dip)   (active-high opto, IPOL inverted)
 *   IO7 - Full beam (high) (active-high opto, IPOL inverted)
 *
 * Digital outputs (Port A, active high = relay on):
 *   GPA2 - Wading mode relay
 *   GPA3 - Fan low speed relay
 *   GPA4 - Fan high speed relay
 *
 * Provides:
 *   - Debounced digital input polling with state-change callbacks
 *   - 4-channel ADC voltage readings
 *   - Compass heading from the magnetometer
 */

#ifndef EXPANSION_BOARD_H
#define EXPANSION_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*******************************************************************************
 * Digital Input Definitions
 ******************************************************************************/

/** Digital input identifiers (active high) */
typedef enum {
    EXBD_INPUT_SELECT     = 0,  // IO0 - Reserved for future use
    EXBD_INPUT_IGNITION   = 1,  // IO1 - Ignition on
    EXBD_INPUT_LIGHTS     = 2,  // IO2 - Lights on
    EXBD_INPUT_FAN_LOW    = 3,  // IO3 - Fan low speed
    EXBD_INPUT_FAN_HIGH   = 4,  // IO4 - Fan high speed
    EXBD_INPUT_COOLANT_LO = 5,  // IO5 - Coolant low warning
    EXBD_INPUT_LOW_BEAM   = 6,  // IO6 - Low beam (dip) headlights
    EXBD_INPUT_FULL_BEAM  = 7,  // IO7 - Full beam (high) headlights
    EXBD_INPUT_COUNT      = 8,
} exbd_input_t;

/** Digital input state */
typedef struct {
    bool current;       // Current debounced state (true = active/high)
    bool changed;       // State changed since last poll
    uint32_t on_time;   // Timestamp (ms) when input last went high
} exbd_input_state_t;

/** All digital inputs state snapshot */
typedef struct {
    exbd_input_state_t inputs[EXBD_INPUT_COUNT];
    uint8_t raw_byte;   // Raw byte from MCP23017 Port A
} exbd_inputs_snapshot_t;

/*******************************************************************************
 * ADC Channel Definitions
 ******************************************************************************/

/** ADC channel identifiers */
typedef enum {
    EXBD_ADC_CH0 = 0,
    EXBD_ADC_CH1 = 1,
    EXBD_ADC_CH2 = 2,
    EXBD_ADC_CH3 = 3,
    EXBD_ADC_COUNT = 4,
} exbd_adc_channel_t;

/*******************************************************************************
 * Callback Types
 ******************************************************************************/

/**
 * @brief Callback for digital input state changes
 * @param input Which input changed
 * @param state New state (true = active high)
 */
typedef void (*exbd_input_callback_t)(exbd_input_t input, bool state);

/*******************************************************************************
 * API
 ******************************************************************************/

/**
 * @brief Initialize the expansion board
 *
 * Initializes the MCP23017, ADS1115, and QMC5883L. Starts the polling task
 * that reads digital inputs at ~20 Hz with 50ms debounce.
 *
 * Call after I2C_Init() in Driver_Init().
 *
 * @return ESP_OK on success. Individual chip failures are logged but don't
 *         prevent other chips from initializing.
 */
esp_err_t expansion_board_init(void);

/**
 * @brief Check if the expansion board was detected during init
 * @return true if at least one device was found
 */
bool expansion_board_detected(void);

/**
 * @brief Check if the MCP23017 I/O expander is available
 * @return true if MCP23017 was found (digital inputs/outputs work)
 */
bool exbd_has_io(void);

/**
 * @brief Probe and reinitialize the expansion board
 *
 * Checks if the expansion board I2C devices are responding (i.e. board has
 * power again after ignition-on). If found, reinitializes all devices.
 *
 * @return true if the board was found and reinitialized
 */
bool expansion_board_probe(void);

/*******************************************************************************
 * Digital Inputs
 ******************************************************************************/

/**
 * @brief Get the debounced state of a single input
 * @param input Input identifier
 * @return true if active (high), false if inactive
 */
bool exbd_get_input(exbd_input_t input);

/**
 * @brief Get a snapshot of all digital input states
 * @param snapshot Pointer to store the snapshot
 */
void exbd_get_inputs(exbd_inputs_snapshot_t *snapshot);

/**
 * @brief Check if the select button was pressed (edge-triggered)
 *
 * Returns true once per press (rising edge). Subsequent calls return false
 * until the button is released and pressed again.
 *
 * @return true if select button was just pressed
 */
bool exbd_select_pressed(void);

/**
 * @brief Register a callback for input state changes
 *
 * The callback is called from the polling task context when any input
 * changes state (after debouncing).
 *
 * @param callback Function to call on state change, or NULL to disable
 */
void exbd_register_input_callback(exbd_input_callback_t callback);

/*******************************************************************************
 * ADC (ADS1115)
 ******************************************************************************/

/**
 * @brief Read an ADC channel voltage
 * @param channel Channel 0-3
 * @param voltage Pointer to store voltage in volts
 * @return ESP_OK on success
 */
esp_err_t exbd_read_adc(exbd_adc_channel_t channel, float *voltage);

/**
 * @brief Read all 4 ADC channels
 * @param voltages Array of 4 floats to store voltages
 * @return ESP_OK on success
 */
esp_err_t exbd_read_all_adc(float voltages[4]);

/*******************************************************************************
 * Magnetometer (QMC5883L)
 ******************************************************************************/

/**
 * @brief Get the compass heading
 * @param heading Pointer to store heading in degrees (0=North, 90=East, etc.)
 * @return ESP_OK on success
 */
esp_err_t exbd_get_heading(float *heading);

/**
 * @brief Get magnetic field strength on all axes (µT)
 * @param x Pointer to store X-axis value (or NULL)
 * @param y Pointer to store Y-axis value (or NULL)
 * @param z Pointer to store Z-axis value (or NULL)
 * @return ESP_OK on success
 */
esp_err_t exbd_get_magnetic_field(float *x, float *y, float *z);

/*******************************************************************************
 * Utility
 ******************************************************************************/

/**
 * @brief Get a human-readable name for a digital input
 * @param input Input identifier
 * @return Static string name (e.g., "Ignition", "Lights")
 */
const char *exbd_input_name(exbd_input_t input);

#ifdef __cplusplus
}
#endif

#endif // EXPANSION_BOARD_H
