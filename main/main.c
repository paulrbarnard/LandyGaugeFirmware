#include "ST77916.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "Wireless.h"
#include "TCA9554PWR.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "PCM5101.h"
#include "CST820.h"
#include "clock.h"
#include "wifi_ntp.h"
#include "artificial_horizon.h"
#include "Tilt/tilt.h"
#include <math.h>


// Gauge selection
typedef enum {
    GAUGE_CLOCK = 0,
    GAUGE_HORIZON,
    GAUGE_TILT,
    GAUGE_COUNT
} gauge_type_t;

static gauge_type_t current_gauge = GAUGE_CLOCK;
static lv_obj_t *touch_overlay = NULL;  // Global overlay reference
static bool test_night_mode = true;     // Track day/night mode for testing

// Forward declarations
void switch_gauge(gauge_type_t new_gauge);
void set_all_gauges_night_mode(bool night_mode);
static void screen_tap_event_handler(lv_event_t * e);  // Forward declare

// Helper function to create the touch overlay
static void create_touch_overlay(void)
{
    lv_obj_t *screen = lv_scr_act();
    touch_overlay = lv_obj_create(screen);
    lv_obj_set_size(touch_overlay, 360, 360);
    lv_obj_set_pos(touch_overlay, 0, 0);
    lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_TRANSP, 0);  // Transparent
    lv_obj_set_style_border_width(touch_overlay, 0, 0);  // No border
    lv_obj_clear_flag(touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_overlay, screen_tap_event_handler, LV_EVENT_PRESSED, NULL);
}

// Touch event handler for gauge cycling
static void screen_tap_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    ESP_LOGI("MAIN", "Touch event code: %d", code);
    
    // Only respond to PRESSED event (finger down) to avoid spam
    if (code == LV_EVENT_PRESSED) {
        // Check if we're about to complete a cycle (going back to first gauge)
        gauge_type_t next_gauge = (current_gauge + 1) % GAUGE_COUNT;
        
        // If cycling back to first gauge, toggle day/night mode
        if (next_gauge == GAUGE_CLOCK && current_gauge != GAUGE_CLOCK) {
            test_night_mode = !test_night_mode;
            ESP_LOGI("MAIN", "Completed gauge cycle - switching to %s mode", 
                     test_night_mode ? "NIGHT" : "DAY");
        }
        
        ESP_LOGI("MAIN", "Touch PRESSED - switching from gauge %d to %d", current_gauge, next_gauge);
        switch_gauge(next_gauge);
    }
}

void switch_gauge(gauge_type_t new_gauge)
{
    ESP_LOGI("MAIN", "switch_gauge called: current=%d, new=%d, night_mode=%d", 
             current_gauge, new_gauge, test_night_mode);
    
    if (new_gauge == current_gauge) {
        ESP_LOGI("MAIN", "Already on gauge %d, ignoring", new_gauge);
        return;
    }
    
    // Hide/cleanup current gauge
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGI("MAIN", "Cleaning up clock");
            clock_cleanup();
            break;
        case GAUGE_HORIZON:
            ESP_LOGI("MAIN", "Cleaning up horizon");
            artificial_horizon_cleanup();
            break;
        // case GAUGE_TILT:
        //     ESP_LOGI("MAIN", "Cleaning up tilt");
        //     tilt_cleanup();
        //     break;
        default:
            break;
    }
    
    // Clear the screen to remove any residual content (this deletes touch_overlay too)
    lv_obj_clean(lv_scr_act());
    touch_overlay = NULL;  // Mark as deleted
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);  // Pure black background
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    lv_obj_invalidate(lv_scr_act());
    ESP_LOGI("MAIN", "Screen cleared");
    
    // Initialize and show new gauge with current night mode setting
    current_gauge = new_gauge;
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGI("MAIN", "Initializing clock (%s mode)", test_night_mode ? "night" : "day");
            clock_init();
            clock_set_night_mode(test_night_mode);
            break;
        case GAUGE_HORIZON:
            ESP_LOGI("MAIN", "Initializing horizon (%s mode)", test_night_mode ? "night" : "day");
            artificial_horizon_init();
            artificial_horizon_set_night_mode(test_night_mode);
            break;
        case GAUGE_TILT:
            ESP_LOGI("MAIN", "Initializing tilt");
            tilt_init();
            // TODO: Add tilt_set_night_mode if implemented
            break;
        default:
            break;
    }
    
    // Recreate touch overlay on top of the new gauge
    create_touch_overlay();
    ESP_LOGI("MAIN", "Touch overlay recreated");
    
    ESP_LOGI("MAIN", "Switched to %s (%s mode)", 
             current_gauge == GAUGE_CLOCK ? "Clock" :
             current_gauge == GAUGE_HORIZON ? "Horizon" :
             current_gauge == GAUGE_TILT ? "Tilt" : "Unknown",
             test_night_mode ? "night" : "day");
}

void Driver_Loop(void *parameter)
{
    Wireless_Init();
    while(1)
    {
        QMI8658_Loop();
        PCF85063_Loop();
        BAT_Get_Volts();
        PWR_Loop();
        
        // Update artificial horizon with IMU data when visible
        if (current_gauge == GAUGE_HORIZON) {
            // Gauge mounted vertically in dash (normal position: X≈1.0g pointing up)
            // X axis points up/down in the vehicle
            // Z axis points forward/back
            // Y axis points left/right
            
            // Calculate pitch from vertical reference:
            // When vertical (normal, X=1, Z=0): pitch=0
            // Top tilts toward driver (Z negative): pitch positive (nose up)
            // Top tilts away (Z positive): pitch negative (nose down)
            float pitch = -atan2f(Accel.z, Accel.x) * 180.0f / M_PI;
            float roll = atan2f(Accel.y, sqrtf(Accel.x * Accel.x + Accel.z * Accel.z)) * 180.0f / M_PI;
            
            artificial_horizon_update(pitch, roll);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}


void Driver_Init(void)
{
    PWR_Init();
    BAT_Init();
    I2C_Init();
    EXIO_Init();                    // Example Initialize EXIO
    Flash_Searching();
    PCF85063_Init();
    QMI8658_Init();
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
    Driver_Init();
    SD_Init();
    Touch_Init();  // Initialize touch BEFORE display to avoid GPIO conflict
    LCD_Init();
    Audio_Init();
    LVGL_Init();   // returns the screen object

    // ********************* Gauge Displays *********************
    // Initialize clock and display with current RTC time (before WiFi/NTP sync)
    clock_init();
    clock_set_visible(true);  // Clock visible by default
    
    // Other gauges will be initialized on-demand when switching
    
    // Initialize WiFi and sync time from NTP in background
    if (wifi_ntp_init()) {
        ESP_LOGI("MAIN", "WiFi connected");
        if (wifi_ntp_sync_time()) {
            ESP_LOGI("MAIN", "Time synchronized from NTP");
        }
    }
    ESP_LOGI("MAIN", "Gauges initialized - Clock visible, use switch_gauge() to change");
    
    // Create a transparent full-screen overlay to catch touch events
    create_touch_overlay();
    ESP_LOGI("MAIN", "Touch overlay created for gauge cycling");
   

    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}






