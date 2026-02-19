/**
 * @file lis3mdl.h
 * @brief LIS3MDL 3-axis magnetometer driver
 *
 * Driver for the ST LIS3MDL digital magnetic sensor on the expansion board.
 * Provides magnetic field readings on X, Y, Z axes and compass heading.
 *
 * I2C address: 0x1C (SDO/SA1 = GND) or 0x1E (SDO/SA1 = VDD)
 */

#ifndef LIS3MDL_H
#define LIS3MDL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*******************************************************************************
 * I2C Configuration
 ******************************************************************************/
#define LIS3MDL_I2C_ADDR        0x1C    // SDO/SA1 = GND (0x1E if SDO/SA1 = VDD)

/*******************************************************************************
 * Register Addresses
 ******************************************************************************/
#define LIS3MDL_REG_WHO_AM_I    0x0F    // Device ID (should read 0x3D)
#define LIS3MDL_REG_CTRL_REG1   0x20    // Control register 1
#define LIS3MDL_REG_CTRL_REG2   0x21    // Control register 2
#define LIS3MDL_REG_CTRL_REG3   0x22    // Control register 3
#define LIS3MDL_REG_CTRL_REG4   0x23    // Control register 4
#define LIS3MDL_REG_CTRL_REG5   0x24    // Control register 5
#define LIS3MDL_REG_STATUS      0x27    // Status register
#define LIS3MDL_REG_OUT_X_L     0x28    // X-axis output LSB
#define LIS3MDL_REG_OUT_X_H     0x29    // X-axis output MSB
#define LIS3MDL_REG_OUT_Y_L     0x2A    // Y-axis output LSB
#define LIS3MDL_REG_OUT_Y_H     0x2B    // Y-axis output MSB
#define LIS3MDL_REG_OUT_Z_L     0x2C    // Z-axis output LSB
#define LIS3MDL_REG_OUT_Z_H     0x2D    // Z-axis output MSB
#define LIS3MDL_REG_TEMP_OUT_L  0x2E    // Temperature output LSB
#define LIS3MDL_REG_TEMP_OUT_H  0x2F    // Temperature output MSB
#define LIS3MDL_REG_INT_CFG     0x30    // Interrupt configuration
#define LIS3MDL_REG_INT_SRC     0x31    // Interrupt source
#define LIS3MDL_REG_INT_THS_L   0x32    // Interrupt threshold LSB
#define LIS3MDL_REG_INT_THS_H   0x33    // Interrupt threshold MSB

/*******************************************************************************
 * WHO_AM_I value
 ******************************************************************************/
#define LIS3MDL_WHO_AM_I_VALUE  0x3D

/*******************************************************************************
 * Status register bits
 ******************************************************************************/
#define LIS3MDL_STATUS_ZYXDA    0x08    // X, Y, Z new data available
#define LIS3MDL_STATUS_XDA      0x01    // X new data
#define LIS3MDL_STATUS_YDA      0x02    // Y new data
#define LIS3MDL_STATUS_ZDA      0x04    // Z new data

/*******************************************************************************
 * CTRL_REG1 settings
 ******************************************************************************/
// TEMP_EN (bit 7): 0 = disabled, 1 = enabled
#define LIS3MDL_TEMP_EN         0x80

// OM[1:0] (bits 6:5): X/Y operating mode
typedef enum {
    LIS3MDL_OM_LOW_POWER   = 0x00,  // Low-power mode
    LIS3MDL_OM_MEDIUM      = 0x20,  // Medium performance
    LIS3MDL_OM_HIGH        = 0x40,  // High performance
    LIS3MDL_OM_ULTRA_HIGH  = 0x60,  // Ultra-high performance
} lis3mdl_om_t;

// DO[2:0] (bits 4:2): Output data rate
typedef enum {
    LIS3MDL_ODR_0_625HZ = 0x00,    //  0.625 Hz
    LIS3MDL_ODR_1_25HZ  = 0x04,    //  1.25 Hz
    LIS3MDL_ODR_2_5HZ   = 0x08,    //  2.5 Hz
    LIS3MDL_ODR_5HZ     = 0x0C,    //  5 Hz
    LIS3MDL_ODR_10HZ    = 0x10,    // 10 Hz
    LIS3MDL_ODR_20HZ    = 0x14,    // 20 Hz
    LIS3MDL_ODR_40HZ    = 0x18,    // 40 Hz
    LIS3MDL_ODR_80HZ    = 0x1C,    // 80 Hz
} lis3mdl_odr_t;

/*******************************************************************************
 * CTRL_REG2 settings
 ******************************************************************************/
// FS[1:0] (bits 6:5): Full-scale selection
typedef enum {
    LIS3MDL_FS_4G  = 0x00,     // ±4 gauss
    LIS3MDL_FS_8G  = 0x20,     // ±8 gauss
    LIS3MDL_FS_12G = 0x40,     // ±12 gauss
    LIS3MDL_FS_16G = 0x60,     // ±16 gauss
} lis3mdl_fs_t;

// REBOOT (bit 3), SOFT_RST (bit 2)
#define LIS3MDL_REBOOT          0x08
#define LIS3MDL_SOFT_RST        0x04

/*******************************************************************************
 * CTRL_REG3 settings
 ******************************************************************************/
// MD[1:0] (bits 1:0): System operating mode
typedef enum {
    LIS3MDL_MD_CONTINUOUS = 0x00,   // Continuous conversion
    LIS3MDL_MD_SINGLE     = 0x01,   // Single conversion
    LIS3MDL_MD_POWER_DOWN = 0x03,   // Power-down
} lis3mdl_md_t;

/*******************************************************************************
 * Data structures
 ******************************************************************************/

/** Raw magnetometer reading */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} lis3mdl_raw_t;

/** Calibrated magnetometer reading in microtesla */
typedef struct {
    float x;    // µT
    float y;    // µT
    float z;    // µT
} lis3mdl_data_t;

/** Hard-iron + soft-iron calibration data */
typedef struct {
    int16_t x_offset;
    int16_t y_offset;
    int16_t z_offset;
    float   scale_x;       /* soft-iron half-spread X (>0) */
    float   scale_y;       /* soft-iron half-spread Y (>0) */
} lis3mdl_calibration_t;

/*******************************************************************************
 * API
 ******************************************************************************/

/**
 * @brief Initialize the LIS3MDL magnetometer
 *
 * Configures: continuous mode, 10 Hz ODR, ±4 gauss, high-performance mode,
 * temperature sensor enabled.
 *
 * @return ESP_OK on success
 */
esp_err_t lis3mdl_init(void);

/**
 * @brief Read raw magnetometer data
 * @param raw Pointer to store the raw X, Y, Z values
 * @return ESP_OK on success
 */
esp_err_t lis3mdl_read_raw(lis3mdl_raw_t *raw);

/**
 * @brief Read calibrated magnetometer data in microtesla
 * @param data Pointer to store the calibrated X, Y, Z values in µT
 * @return ESP_OK on success
 */
esp_err_t lis3mdl_read_data(lis3mdl_data_t *data);

/**
 * @brief Get compass heading in degrees (0-360)
 *
 * Calculates heading from X and Y axes. Sensor should be level.
 *
 * @param heading Pointer to store heading (0=North, 90=East, etc.)
 * @return ESP_OK on success
 */
esp_err_t lis3mdl_get_heading(float *heading);

/**
 * @brief Set hard-iron calibration offsets
 * @param cal Calibration offsets
 */
void lis3mdl_set_calibration(const lis3mdl_calibration_t *cal);

/**
 * @brief Get current calibration values (offsets + scale)
 * @param cal Pointer to store calibration data
 */
void lis3mdl_get_calibration(lis3mdl_calibration_t *cal);

/**
 * @brief Read the chip temperature
 * @param temp_c Pointer to store temperature in °C
 * @return ESP_OK on success
 */
esp_err_t lis3mdl_read_temperature(float *temp_c);

/**
 * @brief Check if new data is ready
 * @return true if XYZ data ready
 */
bool lis3mdl_data_ready(void);

/**
 * @brief Start live hard-iron calibration
 *
 * Begins tracking min/max raw values on X, Y, Z axes.
 * The user should slowly rotate the device through a full 360° turn
 * (ideally in multiple orientations). Call lis3mdl_stop_calibration()
 * when done to compute and apply offsets.
 */
void lis3mdl_start_calibration(void);

/**
 * @brief Stop calibration and apply computed offsets
 *
 * Computes hard-iron offsets as the midpoint of each axis's min/max range
 * and applies them immediately. Requires at least 10 samples.
 */
void lis3mdl_stop_calibration(void);

/**
 * @brief Check if calibration is in progress
 * @return true if calibrating
 */
bool lis3mdl_is_calibrating(void);

/**
 * @brief Get number of calibration samples collected
 * @return Sample count
 */
int lis3mdl_get_cal_sample_count(void);

/**
 * @brief Get calibration progress (0.0 to 1.0)
 *
 * Based on the magnetic field spread captured on X and Y axes.
 * Reaches 1.0 when sufficient rotation has been detected.
 *
 * @return Progress fraction (0.0 = not started, 1.0 = complete)
 */
float lis3mdl_get_cal_progress(void);

/**
 * @brief Check if calibration quality is sufficient
 * @return true if enough spread has been captured on both axes
 */
bool lis3mdl_cal_is_good(void);

#ifdef __cplusplus
}
#endif

#endif // LIS3MDL_H
