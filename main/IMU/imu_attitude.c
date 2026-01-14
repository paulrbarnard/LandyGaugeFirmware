/**
 * @file imu_attitude.c
 * @brief IMU attitude calculation implementation
 * 
 * Computes pitch and roll angles from QMI8658 accelerometer data.
 * The gauge is mounted vertically in the dash:
 * - X axis points up/down (normal position: X ≈ 1.0g pointing up)
 * - Z axis points forward/back  
 * - Y axis points left/right
 */

#include "imu_attitude.h"
#include "QMI8658/QMI8658.h"
#include <math.h>

// Current attitude values (updated by imu_update_attitude)
static float current_pitch = 0.0f;
static float current_roll = 0.0f;

float imu_get_pitch(void)
{
    return current_pitch;
}

float imu_get_roll(void)
{
    return current_roll;
}

void imu_update_attitude(void)
{
    // Gauge mounted vertically in dash (normal position: X≈1.0g pointing up)
    // X axis points up/down in the vehicle
    // Z axis points forward/back
    // Y axis points left/right
    
    // Calculate pitch from vertical reference:
    // When vertical (normal, X=1, Z=0): pitch=0
    // Top tilts toward driver (Z negative): pitch positive (nose up)
    // Top tilts away (Z positive): pitch negative (nose down)
    current_pitch = -atan2f(Accel.z, Accel.x) * 180.0f / M_PI;
    
    // Calculate roll:
    // Positive roll = tilted right (Y positive)
    // Negative roll = tilted left (Y negative)
    current_roll = atan2f(Accel.y, sqrtf(Accel.x * Accel.x + Accel.z * Accel.z)) * 180.0f / M_PI;
}
