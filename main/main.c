#include "ST77916.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "Wireless.h"
#include "TCA9554PWR.h"
#include "LVGL_Example.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "PCM5101.h"
#include "MIC_Speech.h"
#include "CST820.h"
#include "clock.h"
#include "wifi_ntp.h"
#include "artificial_horizon.h"
#include <math.h>


// Gauge selection
typedef enum {
    GAUGE_CLOCK = 0,
    GAUGE_HORIZON,
    GAUGE_COUNT
} gauge_type_t;

static gauge_type_t current_gauge = GAUGE_CLOCK;
static lv_obj_t *touch_overlay = NULL;  // Global overlay reference  // Default to clock on reset

// Forward declaration
void switch_gauge(gauge_type_t new_gauge);

// Touch event handler for gauge cycling
static void screen_tap_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    ESP_LOGI("MAIN", "Touch event code: %d", code);
    
    // Only respond to PRESSED event (finger down) to avoid spam
    if (code == LV_EVENT_PRESSED) {
        // Cycle to next gauge
        gauge_type_t next_gauge = (current_gauge + 1) % GAUGE_COUNT;
        ESP_LOGI("MAIN", "Touch PRESSED - switching from gauge %d to %d", current_gauge, next_gauge);
        switch_gauge(next_gauge);
    }
}

void switch_gauge(gauge_type_t new_gauge)
{
    ESP_LOGI("MAIN", "switch_gauge called: current=%d, new=%d", current_gauge, new_gauge);
    
    if (new_gauge == current_gauge) {
        ESP_LOGI("MAIN", "Already on gauge %d, ignoring", new_gauge);
        return;
    }
    
    // Hide current gauge
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGI("MAIN", "Hiding clock");
            clock_set_visible(false);
            break;
        case GAUGE_HORIZON:
            ESP_LOGI("MAIN", "Hiding horizon");
            artificial_horizon_set_visible(false);
            break;
        default:
            break;
    }
    
    // Show new gauge
    current_gauge = new_gauge;
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGI("MAIN", "Showing clock");
            clock_set_visible(true);
            break;
        case GAUGE_HORIZON:
            ESP_LOGI("MAIN", "Showing horizon");
            artificial_horizon_set_visible(true);
            break;
        default:
            break;
    }
    
    // Bring touch overlay back to front after gauge switch
    if (touch_overlay != NULL) {
        lv_obj_move_foreground(touch_overlay);
        // Also ensure it's clickable
        lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);
        ESP_LOGI("MAIN", "Touch overlay moved to foreground");
    }
    
    ESP_LOGI("MAIN", "Switched to %s", 
             current_gauge == GAUGE_CLOCK ? "Clock" : "Horizon");
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
    // MIC_Speech_init();  // Wake word: "Hi ESP" - TODO: Fix model partition format
    // Play_Music("/sdcard","AAA.mp3");
    LVGL_Init();   // returns the screen object

// /********************* Gauge Displays *********************/
    // Initialize clock and display with current RTC time (before WiFi/NTP sync)
    clock_init();
    clock_set_visible(true);  // Clock visible by default
    
    // Initialize artificial horizon (hidden by default)
    artificial_horizon_init();
    artificial_horizon_set_visible(false);
    
    // Initialize WiFi and sync time from NTP in background
    if (wifi_ntp_init()) {
        ESP_LOGI("MAIN", "WiFi connected");
        if (wifi_ntp_sync_time()) {
            ESP_LOGI("MAIN", "Time synchronized from NTP");
        }
    }
    
    ESP_LOGI("MAIN", "Gauges initialized - Clock visible, use switch_gauge() to change");
    
    // Create a transparent full-screen overlay to catch touch events
    lv_obj_t *screen = lv_scr_act();
    touch_overlay = lv_obj_create(screen);
    lv_obj_set_size(touch_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(touch_overlay, 0, 0);
    lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_TRANSP, 0);  // Transparent
    lv_obj_set_style_border_width(touch_overlay, 0, 0);  // No border
    lv_obj_clear_flag(touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_overlay, screen_tap_event_handler, LV_EVENT_PRESSED, NULL);
    ESP_LOGI("MAIN", "Touch overlay created for gauge cycling");
    // Simulated_Touch_Init();  // Disabled - using real CST820 touch now
    // lv_demo_widgets();
    // lv_demo_keypad_encoder();
    // lv_demo_benchmark();
    // lv_demo_stress();
    // lv_demo_music();

    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}






