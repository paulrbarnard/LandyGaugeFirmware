/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file ads1115.h
 * @brief ADS1115 16-bit 4-channel I2C ADC driver
 *
 * Driver for the Texas Instruments ADS1115 on the expansion board.
 * Supports single-ended and differential measurements with
 * programmable gain amplifier (PGA) and sample rate.
 *
 * I2C address: 0x48 (ADDR pin → GND, default)
 */

#ifndef ADS1115_H
#define ADS1115_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*******************************************************************************
 * I2C Configuration
 ******************************************************************************/
#define ADS1115_I2C_ADDR        0x48    // ADDR → GND (default)

/*******************************************************************************
 * Register Addresses
 ******************************************************************************/
#define ADS1115_REG_CONVERSION  0x00    // Conversion result
#define ADS1115_REG_CONFIG      0x01    // Configuration
#define ADS1115_REG_LO_THRESH   0x02    // Low threshold
#define ADS1115_REG_HI_THRESH   0x03    // High threshold

/*******************************************************************************
 * Configuration Register Bits
 ******************************************************************************/

// Operational status / single-shot start (bit 15)
#define ADS1115_OS_START        0x8000  // Write: start single conversion
#define ADS1115_OS_BUSY         0x0000  // Read: conversion in progress
#define ADS1115_OS_READY        0x8000  // Read: conversion complete

// Input multiplexer (bits 14:12)
typedef enum {
    ADS1115_MUX_DIFF_01  = 0x0000,  // AIN0 - AIN1 (default)
    ADS1115_MUX_DIFF_03  = 0x1000,  // AIN0 - AIN3
    ADS1115_MUX_DIFF_13  = 0x2000,  // AIN1 - AIN3
    ADS1115_MUX_DIFF_23  = 0x3000,  // AIN2 - AIN3
    ADS1115_MUX_SINGLE_0 = 0x4000,  // AIN0 vs GND
    ADS1115_MUX_SINGLE_1 = 0x5000,  // AIN1 vs GND
    ADS1115_MUX_SINGLE_2 = 0x6000,  // AIN2 vs GND
    ADS1115_MUX_SINGLE_3 = 0x7000,  // AIN3 vs GND
} ads1115_mux_t;

// Programmable gain amplifier (bits 11:9)
typedef enum {
    ADS1115_PGA_6144 = 0x0000,  // ±6.144V (not for signals > VDD)
    ADS1115_PGA_4096 = 0x0200,  // ±4.096V (not for signals > VDD)
    ADS1115_PGA_2048 = 0x0400,  // ±2.048V (default)
    ADS1115_PGA_1024 = 0x0600,  // ±1.024V
    ADS1115_PGA_0512 = 0x0800,  // ±0.512V
    ADS1115_PGA_0256 = 0x0A00,  // ±0.256V
} ads1115_pga_t;

// Operating mode (bit 8)
#define ADS1115_MODE_CONTINUOUS 0x0000  // Continuous conversion
#define ADS1115_MODE_SINGLE     0x0100  // Single-shot (default, power-down after)

// Data rate (bits 7:5)
typedef enum {
    ADS1115_DR_8SPS   = 0x0000,  //   8 samples/sec
    ADS1115_DR_16SPS  = 0x0020,  //  16 samples/sec
    ADS1115_DR_32SPS  = 0x0040,  //  32 samples/sec
    ADS1115_DR_64SPS  = 0x0060,  //  64 samples/sec
    ADS1115_DR_128SPS = 0x0080,  // 128 samples/sec (default)
    ADS1115_DR_250SPS = 0x00A0,  // 250 samples/sec
    ADS1115_DR_475SPS = 0x00C0,  // 475 samples/sec
    ADS1115_DR_860SPS = 0x00E0,  // 860 samples/sec
} ads1115_dr_t;

// Comparator mode (bit 4)
#define ADS1115_COMP_TRADITIONAL 0x0000
#define ADS1115_COMP_WINDOW      0x0010

// Comparator polarity (bit 3)
#define ADS1115_COMP_POL_LOW     0x0000  // Active low (default)
#define ADS1115_COMP_POL_HIGH    0x0008  // Active high

// Latching comparator (bit 2)
#define ADS1115_COMP_NONLATCH    0x0000  // Non-latching (default)
#define ADS1115_COMP_LATCH       0x0004  // Latching

// Comparator queue / disable (bits 1:0)
#define ADS1115_COMP_QUE_1       0x0000  // Assert after 1 conversion
#define ADS1115_COMP_QUE_2       0x0001  // Assert after 2 conversions
#define ADS1115_COMP_QUE_4       0x0002  // Assert after 4 conversions
#define ADS1115_COMP_QUE_DISABLE 0x0003  // Disable comparator (default)

/*******************************************************************************
 * API
 ******************************************************************************/

/**
 * @brief Initialize the ADS1115
 *
 * Verifies the device is present on the I2C bus and sets default configuration:
 * single-shot mode, ±4.096V range, 128 SPS.
 *
 * @return ESP_OK on success
 */
esp_err_t ads1115_init(void);

/**
 * @brief Read a single-ended channel (AINx vs GND)
 * @param channel Channel number 0-3
 * @param voltage Pointer to store the voltage reading (in volts)
 * @return ESP_OK on success
 */
esp_err_t ads1115_read_single(uint8_t channel, float *voltage);

/**
 * @brief Read a differential channel pair
 * @param mux Multiplexer setting (use ADS1115_MUX_DIFF_xx constants)
 * @param voltage Pointer to store the voltage reading (in volts)
 * @return ESP_OK on success
 */
esp_err_t ads1115_read_differential(ads1115_mux_t mux, float *voltage);

/**
 * @brief Read raw 16-bit conversion value
 * @param mux Multiplexer setting
 * @param raw Pointer to store the raw 16-bit signed value
 * @return ESP_OK on success
 */
esp_err_t ads1115_read_raw(ads1115_mux_t mux, int16_t *raw);

/**
 * @brief Set the PGA gain
 * @param pga Gain setting
 */
void ads1115_set_gain(ads1115_pga_t pga);

/**
 * @brief Get the current full-scale voltage for the configured PGA
 * @return Full-scale voltage in volts
 */
float ads1115_get_full_scale(void);

/**
 * @brief Set the data rate
 * @param dr Data rate setting
 */
void ads1115_set_data_rate(ads1115_dr_t dr);

#ifdef __cplusplus
}
#endif

#endif // ADS1115_H
