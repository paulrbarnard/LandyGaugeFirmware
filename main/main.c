#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "TCA9554PWR.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "ST7701S.h"
#include "CST820.h"
#include "SD_MMC.h"
#include "LVGL_Driver.h"
#include "LVGL_Example.h"
#include "Wireless.h"
#include "clock.h"
#include "wifi_ntp.h"
#include "artificial_horizon.h"
#include "warning_beep.h"
#include <math.h>

// Gauge selection
typedef enum {
    GAUGE_CLOCK = 0,
    GAUGE_ARTIFICIAL_HORIZON,
    GAUGE_SPEEDOMETER,      // To be implemented
    GAUGE_TACHOMETER,       // To be implemented
    GAUGE_FUEL,             // To be implemented
    GAUGE_TEMPERATURE,      // To be implemented
    GAUGE_COUNT             // Number of gauges
} gauge_type_t;

// Set which gauge to display (change this to switch gauges)
static gauge_type_t current_gauge = GAUGE_CLOCK;

// Global day/night mode setting (false = day mode, true = night mode)
static bool night_mode_enabled = false;

// Attitude estimation for artificial horizon
#define ALPHA 0.50f  // Complementary filter weight (lower = more responsive, less smooth)
#define DT 0.01f     // Sample time (100Hz)
static float pitch_angle = 0.0f;
static float roll_angle = 0.0f;
static bool horizon_initialized = false;

void Driver_Loop(void *parameter)
{
    while(1)
    {
        QMI8658_Loop();
        RTC_Loop();
        BAT_Get_Volts();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

/**
 * @brief Calculate pitch and roll from accelerometer data
 */
static void calculate_attitude_from_accel(IMUdata *accel, float *pitch, float *roll)
{
    float ax = accel->x;
    float ay = accel->y;
    float az = accel->z;
    
    // Calculate pitch using atan2 to get correct sign from az
    // When az is negative (top tilts away), pitch is positive (nose down)
    // When az is positive (top tilts toward), pitch is negative (nose up)
    *pitch = atan2f(-az, ax) * 180.0f / M_PI;
    
    // For roll when device is vertical, use atan2(ay, ax) instead of atan2(ay, az)
    // This avoids the singularity when az is near zero
    *roll = atan2f(-ay, ax) * 180.0f / M_PI;
}

/**
 * @brief Update attitude using complementary filter
 */
static void update_artificial_horizon_attitude(void)
{
    // Initialize on first run with current accelerometer reading
    if (!horizon_initialized) {
        calculate_attitude_from_accel(&Accel, &pitch_angle, &roll_angle);
        // Check for valid values
        if (isnan(pitch_angle) || isnan(roll_angle)) {
            pitch_angle = 90.0f;  // Default to vertical
            roll_angle = 0.0f;
        }
        horizon_initialized = true;
        ESP_LOGI("MAIN", "Horizon initialized: pitch=%.2f, roll=%.2f", pitch_angle, roll_angle);
        artificial_horizon_update(pitch_angle, roll_angle);
        return;
    }
    
    // Get accelerometer pitch and roll
    float accel_pitch, accel_roll;
    calculate_attitude_from_accel(&Accel, &accel_pitch, &accel_roll);
    
    // Use gyro for pitch (smooth and responsive), not for roll (drifts)
    pitch_angle += -Gyro.x * DT;
    // roll_angle += -Gyro.y * DT;  // Disabled - drifts without calibration
    
    // Apply complementary filter
    pitch_angle = ALPHA * pitch_angle + (1.0f - ALPHA) * accel_pitch;
    roll_angle = ALPHA * roll_angle + (1.0f - ALPHA) * accel_roll;
    
    // Prevent NaN propagation
    if (isnan(pitch_angle)) pitch_angle = accel_pitch;
    if (isnan(roll_angle)) roll_angle = accel_roll;
    
    // Display pitch directly (no offset needed with new calculation)
    float display_pitch = pitch_angle;
    // Invert roll so arrow points up as reference
    float display_roll = -roll_angle;
    
    // Update the display
    artificial_horizon_update(display_pitch, display_roll);
}

/**
 * @brief Initialize all gauges
 */
static void gauges_init(void)
{
    ESP_LOGI("MAIN", "Initializing gauges...");
    
    // Initialize warning beep system
    warning_beep_init();
    
    // Initialize clock (always needed for background time sync)
    clock_init();
    clock_set_night_mode(night_mode_enabled);
    
    // Initialize artificial horizon
    artificial_horizon_init();
    artificial_horizon_set_night_mode(night_mode_enabled);
    
    // Hide all gauges initially
    clock_set_visible(false);
    artificial_horizon_set_visible(false);
    
    // Show only the selected gauge
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGI("MAIN", "Displaying: Analog Clock");
            clock_set_visible(true);
            break;
            
        case GAUGE_ARTIFICIAL_HORIZON:
            ESP_LOGI("MAIN", "Displaying: Artificial Horizon");
            artificial_horizon_set_visible(true);
            break;
            
        case GAUGE_SPEEDOMETER:
        case GAUGE_TACHOMETER:
        case GAUGE_FUEL:
        case GAUGE_TEMPERATURE:
            ESP_LOGI("MAIN", "Gauge not yet implemented, showing clock");
            current_gauge = GAUGE_CLOCK;
            clock_set_visible(true);
            break;
            
        default:
            ESP_LOGI("MAIN", "Unknown gauge, showing clock");
            current_gauge = GAUGE_CLOCK;
            clock_set_visible(true);
            break;
    }
    
    ESP_LOGI("MAIN", "Gauges initialized");
}

/**
 * @brief Update the currently displayed gauge
 */
static void update_current_gauge(void)
{
    switch (current_gauge) {
        case GAUGE_CLOCK:
            clock_update(datetime.hour, datetime.minute, datetime.second);
            break;
            
        case GAUGE_ARTIFICIAL_HORIZON:
            update_artificial_horizon_attitude();
            break;
            
        case GAUGE_SPEEDOMETER:
        case GAUGE_TACHOMETER:
        case GAUGE_FUEL:
        case GAUGE_TEMPERATURE:
            // To be implemented
            break;
            
        default:
            break;
    }
}

void Driver_Init(void)
{
    Flash_Searching();
    BAT_Init();
    I2C_Init();
    PCF85063_Init();
    QMI8658_Init();
    EXIO_Init();                    // Example Initialize EXIO
    xTaskCreatePinnedToCore(
        Driver_Loop, 
        "Other Driver task",
        4096, 
        NULL, 
        3, 
        NULL, 
        0);
}
void app_main(void)
{   
    Wireless_Init();
    Driver_Init();
    LCD_Init();
    Touch_Init();
    SD_Init();
    LVGL_Init();

    // Initialize all gauges
    gauges_init();
   
    // Connect to WiFi and sync time
    ESP_LOGI("MAIN", "Connecting to WiFi...");
    if (wifi_ntp_init()) {
        ESP_LOGI("MAIN", "WiFi connected successfully");
        
        // Sync time from NTP and update RTC
        ESP_LOGI("MAIN", "Synchronizing time from NTP...");
        if (wifi_ntp_sync_time()) {
            ESP_LOGI("MAIN", "Time synchronized successfully");
        } else {
            ESP_LOGE("MAIN", "Failed to synchronize time");
        }
    } else {
        ESP_LOGE("MAIN", "WiFi connection failed");
    }
    

    while (1) {
        // Update the currently displayed gauge
        update_current_gauge();
        
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}
