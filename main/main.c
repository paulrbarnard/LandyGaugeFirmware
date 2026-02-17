#include "ST77916.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "Wireless.h"
#include "TCA9554PWR.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "PCM5101.h"
#include "button_input.h"
#include "CST820.h"  // Always include - runtime detection
#include "clock.h"
#include "wifi_ntp.h"
#include "artificial_horizon.h"
#include "Tilt/tilt.h"
#include "TirePressure/tire_pressure.h"
#include "IMU/imu_attitude.h"
#include "BLE_TPMS/ble_tpms.h"
#include "Boost/boost.h"
#include "expansion_board.h"
#include "ads1115.h"
#include <math.h>


// Gauge selection
typedef enum {
    GAUGE_CLOCK = 0,
    GAUGE_HORIZON,
    GAUGE_TILT,
    GAUGE_TIRE_PRESSURE,
    GAUGE_BOOST,
    GAUGE_COUNT
} gauge_type_t;

static gauge_type_t current_gauge = GAUGE_CLOCK;
static lv_obj_t *touch_overlay = NULL;  // Global overlay reference (used only if touch available)
static bool test_night_mode = false;    // Track day/night mode for testing

// Auto-switch thresholds (use yellow warning levels)
#define AUTO_SWITCH_ROLL_THRESHOLD  30.0f  // Switch to tilt gauge
#define AUTO_SWITCH_PITCH_THRESHOLD 35.0f  // Switch to horizon gauge
#define AUTO_SWITCH_LOCKOUT_MS      5000   // 5 second lockout after manual switch
#define INACTIVITY_TIMEOUT_MS       300000 // 5 minutes = 300,000 ms

static uint32_t manual_switch_time = 0;    // Timestamp of last manual switch
static uint32_t last_activity_time = 0;    // Timestamp of last activity (for auto-return to clock)
static bool auto_switch_enabled = false;   // Disabled for now - manual switching only (set true to re-enable)

// Thread-safe pending switch request (Driver_Loop sets, main loop handles)
static volatile gauge_type_t pending_switch = GAUGE_COUNT;  // GAUGE_COUNT = no pending switch

// MAP sensor configuration (2-bar boost Renault sensor on ADS1115 AIN0)
// "2 bar" = 2 bar gauge boost = 3 bar absolute (0-300 kPa)
// Sensor outputs 0-5V, resistor divider R26=47K / R28=33K scales to 0-2.0625V at ADC
// Divider ratio = 33/(47+33) = 0.4125
#define MAP_ADC_CHANNEL         0       // AIN0
#define MAP_DIVIDER_RATIO       0.4125f // R28/(R26+R28) = 33K/80K
#define MAP_VOLTAGE_MIN         (0.0f * MAP_DIVIDER_RATIO)   // 0V sensor = 0 kPa
#define MAP_VOLTAGE_MAX         (5.0f * MAP_DIVIDER_RATIO)   // 5V sensor = 300 kPa (2.0625V at ADC)
#define MAP_KPA_MIN             0.0f    // 0 kPa absolute
#define MAP_KPA_MAX             300.0f  // 300 kPa absolute (3 bar abs = 2 bar boost)
#define MAP_ATMOSPHERIC_KPA     101.325f // Standard atmosphere
#define KPA_TO_PSI              0.145038f
static bool map_adc_configured = false;  // Track if PGA/DR set for MAP reading

// Forward declarations
void switch_gauge(gauge_type_t new_gauge);
void set_all_gauges_night_mode(bool night_mode);
static void screen_tap_event_handler(lv_event_t * e);  // Forward declare

// Forward declaration for input handling
static void handle_next_gauge_input(void);
static void handle_prev_gauge_input(void);

// Helper function to create the touch overlay (only called if touch_available)
static void create_touch_overlay(void)
{
    if (!touch_available) return;  // Runtime check
    
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

// TPMS update callback - links BLE sensor data to tire pressure gauge
static void tpms_update_cb(tpms_position_t position, const tpms_sensor_data_t *data)
{
    // Update the tire pressure gauge with pressure, temperature, and battery
    tire_pressure_set_sensor_data((int)position, data->pressure_psi, 
                                   data->temperature_c, data->battery_percent);
}

// Touch event handler for gauge cycling
static void screen_tap_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    ESP_LOGI("MAIN", "Touch event code: %d", code);
    
    // Only respond to PRESSED event (finger down) to avoid spam
    if (code == LV_EVENT_PRESSED) {
        handle_next_gauge_input();
    }
}

// Common input handler for both touch and button
static void handle_next_gauge_input(void)
{
    // Check if we're about to complete a cycle (going back to first gauge)
    gauge_type_t next_gauge = (current_gauge + 1) % GAUGE_COUNT;
    
    // If cycling back to first gauge, toggle day/night mode
    if (next_gauge == GAUGE_CLOCK && current_gauge != GAUGE_CLOCK) {
        test_night_mode = !test_night_mode;
        ESP_LOGI("MAIN", "Completed gauge cycle - switching to %s mode", 
                 test_night_mode ? "NIGHT" : "DAY");
    }
    
    // Record manual switch time to prevent immediate auto-switch back
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    manual_switch_time = now;
    last_activity_time = now;  // Reset inactivity timer
    
    ESP_LOGI("MAIN", "Input - switching from gauge %d to %d (manual, lockout %dms)", 
             current_gauge, next_gauge, AUTO_SWITCH_LOCKOUT_MS);
    switch_gauge(next_gauge);
}

// Common input handler for previous gauge (backward cycling)
static void handle_prev_gauge_input(void)
{
    gauge_type_t prev_gauge = (current_gauge == 0) ? (GAUGE_COUNT - 1) : (current_gauge - 1);

    // Record manual switch time to prevent immediate auto-switch back
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    manual_switch_time = now;
    last_activity_time = now;

    ESP_LOGI("MAIN", "Input - switching from gauge %d to %d (prev, lockout %dms)",
             current_gauge, prev_gauge, AUTO_SWITCH_LOCKOUT_MS);
    switch_gauge(prev_gauge);
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
        case GAUGE_TIRE_PRESSURE:
            ESP_LOGI("MAIN", "Cleaning up tire pressure");
            tire_pressure_cleanup();
            ble_tpms_set_fast_scan(false);  // Disable fast scan when leaving TPMS gauge
            break;
        case GAUGE_BOOST:
            ESP_LOGI("MAIN", "Cleaning up boost gauge");
            boost_cleanup();
            break;
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
            ESP_LOGI("MAIN", "Initializing tilt (%s mode)", test_night_mode ? "night" : "day");
            tilt_init();
            tilt_set_night_mode(test_night_mode);
            break;
        case GAUGE_TIRE_PRESSURE:
            ESP_LOGI("MAIN", "Initializing tire pressure (%s mode)", test_night_mode ? "night" : "day");
            tire_pressure_init();
            tire_pressure_set_night_mode(test_night_mode);
            ble_tpms_set_fast_scan(true);  // Enable fast scan when viewing TPMS gauge
            break;
        case GAUGE_BOOST:
            ESP_LOGI("MAIN", "Initializing boost gauge (%s mode)", test_night_mode ? "night" : "day");
            boost_init();
            boost_set_night_mode(test_night_mode);
            break;
        default:
            break;
    }
    
    // Recreate touch overlay on top of the new gauge (if touch available)
    if (touch_available) {
        create_touch_overlay();
        ESP_LOGI("MAIN", "Touch overlay recreated");
    }
    
    ESP_LOGI("MAIN", "Switched to %s (%s mode)", 
             current_gauge == GAUGE_CLOCK ? "Clock" :
             current_gauge == GAUGE_HORIZON ? "Horizon" :
             current_gauge == GAUGE_TILT ? "Tilt" :
             current_gauge == GAUGE_TIRE_PRESSURE ? "Tire Pressure" :
             current_gauge == GAUGE_BOOST ? "Boost" : "Unknown",
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
        
        // Update IMU attitude calculations
        imu_update_attitude();
        
        float current_roll = imu_get_roll();
        float current_pitch = imu_get_pitch();
        
        // Update artificial horizon with IMU data when visible
        if (current_gauge == GAUGE_HORIZON) {
            artificial_horizon_update(current_pitch, current_roll);
        }
        
        // Update tilt gauge with IMU roll data when visible
        if (current_gauge == GAUGE_TILT) {
            tilt_set_angle(current_roll);
        }
        
        // Auto-switch to warning gauges based on thresholds
        // Priority: Roll (tilt) > Pitch (horizon) > Tire pressure (future)
        // NOTE: We set pending_switch instead of calling switch_gauge directly
        //       because LVGL is not thread-safe. Main loop handles the actual switch.
        if (auto_switch_enabled) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            bool lockout_active = (now - manual_switch_time) < AUTO_SWITCH_LOCKOUT_MS;
            
            if (!lockout_active) {
                // Highest priority: Roll exceeds threshold -> switch to tilt gauge
                if (current_gauge != GAUGE_TILT && fabsf(current_roll) >= AUTO_SWITCH_ROLL_THRESHOLD) {
                    ESP_LOGW("MAIN", "Auto-switch request: TILT (roll=%.1f exceeds %.1f)",
                             current_roll, AUTO_SWITCH_ROLL_THRESHOLD);
                    last_activity_time = now;  // Reset inactivity timer on alarm
                    pending_switch = GAUGE_TILT;
                }
                // Second priority: Pitch exceeds threshold -> switch to horizon gauge
                else if (current_gauge != GAUGE_HORIZON && fabsf(current_pitch) >= AUTO_SWITCH_PITCH_THRESHOLD) {
                    ESP_LOGW("MAIN", "Auto-switch request: HORIZON (pitch=%.1f exceeds %.1f)",
                             current_pitch, AUTO_SWITCH_PITCH_THRESHOLD);
                    last_activity_time = now;  // Reset inactivity timer on alarm
                    pending_switch = GAUGE_HORIZON;
                }
                // Third priority: Tire pressure rapid drop -> switch to TPMS gauge
                else if (current_gauge != GAUGE_TIRE_PRESSURE && ble_tpms_check_pressure_drop_alarm()) {
                    tpms_position_t pos = ble_tpms_get_pressure_drop_position();
                    ESP_LOGW("MAIN", "Auto-switch request: TIRE_PRESSURE (rapid drop on %s)",
                             ble_tpms_position_str(pos));
                    ble_tpms_clear_pressure_drop_alarm();  // Clear after handling
                    last_activity_time = now;
                    pending_switch = GAUGE_TIRE_PRESSURE;
                }
            }
            
            // Auto-return to clock after inactivity timeout
            if (current_gauge != GAUGE_CLOCK && (now - last_activity_time) >= INACTIVITY_TIMEOUT_MS) {
                ESP_LOGI("MAIN", "Inactivity timeout (%d min) - requesting clock",
                         INACTIVITY_TIMEOUT_MS / 60000);
                last_activity_time = now;  // Reset to prevent repeated switches
                pending_switch = GAUGE_CLOCK;
            }
        }
        
        // Periodic BLE TPMS scan restart (every 30 sec)
        ble_tpms_periodic_update();
        
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
    expansion_board_init();          // Initialize expansion board (MCP23017, ADS1115, QMC5883L)
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
    
    // Try to initialize touch - will auto-detect if touch controller present
    if (Touch_Init()) {
        ESP_LOGI("MAIN", "Touch screen detected and enabled");
    } else {
        ESP_LOGI("MAIN", "No touch screen detected - button-only mode");
    }
    
    LCD_Init();
    Audio_Init();
    button_input_init();  // Initialize button input (works on all versions)
    LVGL_Init();   // returns the screen object

    // ********************* Gauge Displays *********************
    // Initialize clock and display with current RTC time (before WiFi/NTP sync)
    clock_init();
    clock_set_night_mode(test_night_mode);  // Apply initial day/night mode
    clock_set_visible(true);  // Clock visible by default
    
    // Other gauges will be initialized on-demand when switching
    
    // Initialize WiFi and sync time from NTP in background
    if (wifi_ntp_init()) {
        ESP_LOGI("MAIN", "WiFi connected");
        if (wifi_ntp_sync_time()) {
            ESP_LOGI("MAIN", "Time synchronized from NTP");
        }
    }
    
    // Initialize BLE TPMS (using NimBLE - lighter weight than Bluedroid)
    if (ble_tpms_init() == ESP_OK) {
        ESP_LOGI("MAIN", "BLE TPMS initialized (NimBLE)");
        
        // Register known TPMS sensor MAC addresses (AIYATO sensors for Land Rover)
        ble_tpms_register_sensor_str(TPMS_FRONT_LEFT,  "80:EA:CA:50:3A:51");
        ble_tpms_register_sensor_str(TPMS_FRONT_RIGHT, "81:EA:CA:50:3B:6B");
        ble_tpms_register_sensor_str(TPMS_REAR_LEFT,   "82:EA:CA:50:3B:13");
        ble_tpms_register_sensor_str(TPMS_REAR_RIGHT,  "83:EA:CA:50:3B:6C");
        
        // Register callback to update tire pressure gauge display
        ble_tpms_register_callback(tpms_update_cb);
        
        ble_tpms_start_scan();
        ESP_LOGI("MAIN", "BLE TPMS scanning for sensors...");
    } else {
        ESP_LOGE("MAIN", "Failed to initialize BLE TPMS");
    }
    ESP_LOGI("MAIN", "Gauges initialized - Clock visible, use switch_gauge() to change");
    
    // Create touch overlay if touch is available
    if (touch_available) {
        create_touch_overlay();
        ESP_LOGI("MAIN", "Touch overlay created for gauge cycling");
    }

    ESP_LOGI("MAIN", "Button input active on GPIO%d", CONFIG_BUTTON_NEXT_GPIO);

    while (1) {
        // Check for button press (GPIO0 boot button or GPIO43 next)
        if (button_input_pressed()) {
            ESP_LOGI("MAIN", "Next button pressed - switching gauge");
            handle_next_gauge_input();
        }
        
        // Check for previous button press (GPIO44)
        if (button_input_prev_pressed()) {
            ESP_LOGI("MAIN", "Prev button pressed - switching gauge");
            handle_prev_gauge_input();
        }
        
        // Check expansion board select button (if present)
        if (expansion_board_detected() && exbd_select_pressed()) {
            ESP_LOGI("MAIN", "Expansion select button - switching gauge");
            handle_next_gauge_input();
        }
        
        // Auto night mode from expansion board lights input
        if (expansion_board_detected()) {
            bool lights_on = exbd_get_input(EXBD_INPUT_LIGHTS);
            if (lights_on != test_night_mode) {
                test_night_mode = lights_on;
                ESP_LOGI("MAIN", "Lights input → %s mode", lights_on ? "NIGHT" : "DAY");
                // Apply night mode to the currently active gauge
                switch (current_gauge) {
                    case GAUGE_CLOCK:         clock_set_night_mode(test_night_mode); break;
                    case GAUGE_HORIZON:       artificial_horizon_set_night_mode(test_night_mode); break;
                    case GAUGE_TILT:          tilt_set_night_mode(test_night_mode); break;
                    case GAUGE_TIRE_PRESSURE: tire_pressure_set_night_mode(test_night_mode); break;
                    case GAUGE_BOOST:         boost_set_night_mode(test_night_mode); break;
                    default: break;
                }
            }
        }
        
        // Read MAP sensor and update boost gauge (only when boost gauge is active)
        if (current_gauge == GAUGE_BOOST && expansion_board_detected()) {
            // Configure ADC for fast MAP reads on first use
            if (!map_adc_configured) {
                ads1115_set_gain(ADS1115_PGA_2048);    // ±2.048V for 0-2V divider output
                ads1115_set_data_rate(ADS1115_DR_860SPS); // Fast conversion (~2ms)
                map_adc_configured = true;
                ESP_LOGI("MAIN", "MAP ADC configured: ±2.048V, 860 SPS");
            }

            float voltage = 0.0f;
            esp_err_t adc_ret = ads1115_read_single(MAP_ADC_CHANNEL, &voltage);
            if (adc_ret == ESP_OK) {
                // Convert voltage to kPa absolute
                float kpa_abs = MAP_KPA_MIN + 
                    (voltage - MAP_VOLTAGE_MIN) / (MAP_VOLTAGE_MAX - MAP_VOLTAGE_MIN) 
                    * (MAP_KPA_MAX - MAP_KPA_MIN);
                // Clamp to valid range
                if (kpa_abs < MAP_KPA_MIN) kpa_abs = MAP_KPA_MIN;
                if (kpa_abs > MAP_KPA_MAX) kpa_abs = MAP_KPA_MAX;
                // Convert to gauge pressure (boost bar)
                float boost_bar = (kpa_abs - MAP_ATMOSPHERIC_KPA) / 100.0f;
                if (boost_bar < 0.0f) boost_bar = 0.0f;  // No vacuum on diesel gauge

                static int map_log_counter = 0;
                if ((map_log_counter++ % 50) == 0) {  // Log every ~0.5s
                    ESP_LOGI("MAP", "V=%.3f kPa=%.1f BAR=%.2f", voltage, kpa_abs, boost_bar);
                }
                boost_set_value(boost_bar);
            } else {
                static int err_counter = 0;
                if ((err_counter++ % 100) == 0) {
                    ESP_LOGE("MAP", "ADC read failed: %s", esp_err_to_name(adc_ret));
                }
            }
        }

        // Handle pending auto-switch requests from Driver_Loop (thread-safe)
        if (pending_switch != GAUGE_COUNT) {
            gauge_type_t target = pending_switch;
            pending_switch = GAUGE_COUNT;  // Clear before switching
            ESP_LOGI("MAIN", "Processing pending auto-switch to gauge %d", target);
            switch_gauge(target);
        }
        
        // Check for display SPI errors and trigger refresh if needed
        lvgl_check_and_refresh();
        
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}






