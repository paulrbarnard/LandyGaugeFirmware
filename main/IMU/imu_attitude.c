/**
 * @file imu_attitude.c
 * @brief IMU attitude calculation implementation
 * 
 * Computes pitch and roll angles from QMI8658 accelerometer data.
 * The gauge is mounted in the dash tilted back (top away from driver)
 * by MOUNT_ANGLE_DEG degrees from vertical:
 * - X axis points up/down (at mount angle from true vertical)
 * - Z axis points forward/back  
 * - Y axis points left/right
 *
 * The raw accelerometer vector is rotated in the X-Z plane to
 * compensate for the mounting angle before computing attitude.
 */

#include "imu_attitude.h"
#include "QMI8658/QMI8658.h"
#include <math.h>

// Mounting angle compensation: gauge is tilted 20° back (top away from driver)
// Negative value rotates the accel vector to cancel the backward tilt
#define MOUNT_ANGLE_DEG  (-20.0f)
#define MOUNT_ANGLE_RAD  (MOUNT_ANGLE_DEG * (float)M_PI / 180.0f)

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
    // Gauge mounted in dash at MOUNT_ANGLE_DEG from vertical (top tilted back)
    // X axis points along gauge face normal (up when vertical)
    // Z axis points forward/back
    // Y axis points left/right
    
    // Rotate the accelerometer vector in the X-Z plane to compensate
    // for the mounting angle. This transforms the readings as if the
    // gauge were mounted perfectly vertical.
    float cos_m = cosf(MOUNT_ANGLE_RAD);
    float sin_m = sinf(MOUNT_ANGLE_RAD);
    float ax_rot =  Accel.x * cos_m + Accel.z * sin_m;
    float az_rot = -Accel.x * sin_m + Accel.z * cos_m;
    
    // Calculate pitch from rotated vertical reference:
    // When vehicle is level: ax_rot≈1g, az_rot≈0 → pitch=0
    // Top tilts toward driver (nose up): pitch positive
    // Top tilts away (nose down): pitch negative
    current_pitch = -atan2f(az_rot, ax_rot) * 180.0f / M_PI;
    
    // Calculate roll using rotated X and Z (Y axis unaffected by pitch-plane rotation):
    // Positive roll = tilted right (Y positive)
    // Negative roll = tilted left (Y negative)
    current_roll = atan2f(Accel.y, sqrtf(ax_rot * ax_rot + az_rot * az_rot)) * 180.0f / M_PI;
}
