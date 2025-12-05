/**
 * @file artificial_horizon_example.c
 * @brief Example showing how to integrate the artificial horizon with QMI8658 IMU
 */

#include "artificial_horizon.h"
#include "QMI8658.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "HORIZON_EXAMPLE";

// Complementary filter parameters for attitude estimation
#define ALPHA 0.98f  // Weight for gyro integration (0.98 = 98% gyro, 2% accel)
#define DT 0.01f     // Sample time in seconds (100Hz = 0.01s)

// Current estimated attitude
static float pitch_angle = 0.0f;
static float roll_angle = 0.0f;

/**
 * @brief Calculate pitch and roll from accelerometer data
 * 
 * @param accel Accelerometer data structure
 * @param pitch Output pitch angle in degrees
 * @param roll Output roll angle in degrees
 */
static void calculate_attitude_from_accel(IMUdata *accel, float *pitch, float *roll)
{
    // Calculate pitch (rotation around Y axis)
    // pitch = atan2(-ax, sqrt(ay^2 + az^2))
    float ax = accel->x;
    float ay = accel->y;
    float az = accel->z;
    
    *pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI;
    
    // Calculate roll (rotation around X axis)
    // roll = atan2(ay, az)
    *roll = atan2f(ay, az) * 180.0f / M_PI;
}

/**
 * @brief Update attitude using complementary filter
 * 
 * This combines gyroscope integration (for fast response) with
 * accelerometer data (for long-term stability)
 */
void horizon_update_attitude(void)
{
    // Get accelerometer pitch and roll
    float accel_pitch, accel_roll;
    calculate_attitude_from_accel(&Accel, &accel_pitch, &accel_roll);
    
    // Integrate gyroscope data (gyro is in degrees/second)
    // Note: Gyro axes may need to be adjusted based on sensor orientation
    pitch_angle += Gyro.y * DT;  // Pitch rate around Y axis
    roll_angle += Gyro.x * DT;   // Roll rate around X axis
    
    // Apply complementary filter
    // This combines gyro integration (short term) with accel data (long term)
    pitch_angle = ALPHA * pitch_angle + (1.0f - ALPHA) * accel_pitch;
    roll_angle = ALPHA * roll_angle + (1.0f - ALPHA) * accel_roll;
    
    // Update the display
    artificial_horizon_update(pitch_angle, roll_angle);
}

/**
 * @brief Initialize and test the artificial horizon
 */
void horizon_example_init(void)
{
    ESP_LOGI(TAG, "Initializing artificial horizon example");
    
    // Initialize the artificial horizon display
    artificial_horizon_init();
    
    // Set night mode if desired
    // artificial_horizon_set_night_mode(true);
    
    ESP_LOGI(TAG, "Artificial horizon example initialized");
    ESP_LOGI(TAG, "Call horizon_update_attitude() regularly (e.g., 100Hz) to update display");
}

/**
 * @brief Example task that updates the horizon at 100Hz
 */
void horizon_update_task(void *parameter)
{
    const TickType_t update_interval = pdMS_TO_TICKS(10);  // 100Hz = 10ms
    
    while (1) {
        horizon_update_attitude();
        vTaskDelay(update_interval);
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Test function that simulates aircraft motion
 */
void horizon_test_demo(void)
{
    static float test_pitch = 0.0f;
    static float test_roll = 0.0f;
    static float pitch_dir = 1.0f;
    static float roll_dir = 1.0f;
    
    // Simulate slow pitch and roll changes
    test_pitch += pitch_dir * 0.5f;
    if (test_pitch > 30.0f || test_pitch < -30.0f) {
        pitch_dir = -pitch_dir;
    }
    
    test_roll += roll_dir * 1.0f;
    if (test_roll > 45.0f || test_roll < -45.0f) {
        roll_dir = -roll_dir;
    }
    
    artificial_horizon_update(test_pitch, test_roll);
}
