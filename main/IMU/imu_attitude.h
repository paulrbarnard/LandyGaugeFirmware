/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file imu_attitude.h
 * @brief IMU attitude calculation module
 * 
 * Provides pitch and roll calculations from the QMI8658 IMU sensor.
 * Shared by both the Artificial Horizon and Tilt gauges.
 */

#ifndef IMU_ATTITUDE_H
#define IMU_ATTITUDE_H

#include <stdbool.h>

/**
 * @brief Get the current pitch angle
 * 
 * Calculated from accelerometer data. Positive = nose up, negative = nose down.
 * 
 * @return Pitch angle in degrees (-90 to +90)
 */
float imu_get_pitch(void);

/**
 * @brief Get the current roll angle
 * 
 * Calculated from accelerometer data. Positive = tilted right, negative = tilted left.
 * 
 * @return Roll angle in degrees (-180 to +180)
 */
float imu_get_roll(void);

/**
 * @brief Update the attitude calculations from IMU sensor
 * 
 * Call this periodically (e.g., every 100ms) to update the pitch and roll values.
 * Reads accelerometer data from QMI8658 and computes attitude angles.
 */
void imu_update_attitude(void);

#endif // IMU_ATTITUDE_H
