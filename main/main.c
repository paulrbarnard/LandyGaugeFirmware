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
#include "Incline/incline.h"
#include "TirePressure/tire_pressure.h"
#include "IMU/imu_attitude.h"
#include "BLE_TPMS/ble_tpms.h"
#include "Boost/boost.h"
#include "Compass/compass.h"
#include "EGT/egt.h"
#include "Cooling/cooling.h"
#include "expansion_board.h"
#include "ads1115.h"
#include "warning_beep.h"
#include "lis3mdl.h"
#include "mcp9600.h"
#include "settings.h"
#include "settings_screen.h"
#include "user_input.h"
#include "nvs_flash.h"
#include "soc/rtc.h"
#include <math.h>


// Gauge selection
typedef enum {
    GAUGE_CLOCK = 0,
    GAUGE_HORIZON,       // Temporarily skipped in cycling
    GAUGE_TILT,
    GAUGE_INCLINE,
    GAUGE_TIRE_PRESSURE,
    GAUGE_BOOST,
    GAUGE_COMPASS,
    GAUGE_EGT,
    GAUGE_COOLING,
    GAUGE_SETTINGS,      // Hidden — not in gauge_order[]
    GAUGE_COUNT
} gauge_type_t;

static gauge_type_t current_gauge = GAUGE_CLOCK;
static lv_obj_t *touch_overlay = NULL;  // Global overlay reference (used only if touch available)
static bool test_night_mode = false;    // Track day/night mode for testing

// Backlight levels: night mode is dimmer since green is perceived brighter
#define BACKLIGHT_DAY   70
#define BACKLIGHT_NIGHT 15

/* ── Gauge cycling order ─────────────────────────────────────────────── */
static const gauge_type_t gauge_order[] = {
    GAUGE_CLOCK,
    GAUGE_BOOST,
    GAUGE_EGT,
    GAUGE_COOLING,
    GAUGE_TIRE_PRESSURE,
    GAUGE_TILT,
    GAUGE_INCLINE,
    GAUGE_COMPASS,
};
#define GAUGE_ORDER_COUNT  (sizeof(gauge_order) / sizeof(gauge_order[0]))

/* Returns true if the gauge needs the expansion board to be connected */
static bool gauge_needs_expansion(gauge_type_t g)
{
    return (g == GAUGE_BOOST || g == GAUGE_EGT || g == GAUGE_COMPASS || g == GAUGE_COOLING);
}

/* Find the index of a gauge in gauge_order[], or 0 if not found */
static int gauge_order_index(gauge_type_t g)
{
    for (int i = 0; i < (int)GAUGE_ORDER_COUNT; i++) {
        if (gauge_order[i] == g) return i;
    }
    return 0;
}

/* Determine whether a gauge should be skipped in cycling */
static bool should_skip_gauge(gauge_type_t g)
{
    if (g == GAUGE_HORIZON) return true;  /* always skip */
    if (!expansion_board_detected() && gauge_needs_expansion(g)) return true;
    return false;
}

// Auto-switch thresholds (use yellow warning levels)
#define AUTO_SWITCH_ROLL_THRESHOLD  30.0f  // Switch to tilt gauge (yellow zone)
#define AUTO_SWITCH_LOCKOUT_MS      5000   // 5 second lockout after manual/auto switch
#define INACTIVITY_TIMEOUT_MS       600000 // 10 minutes = 600,000 ms
#define ALARM_RETURN_DELAY_MS       30000  // 30 seconds after alarm clears → return to previous gauge

static uint32_t manual_switch_time = 0;    // Timestamp of last manual switch
static uint32_t last_activity_time = 0;    // Timestamp of last activity (for auto-return to clock)

// Alarm auto-return state: return to previous gauge when alarm clears
static gauge_type_t alarm_previous_gauge = GAUGE_COUNT; // Gauge before alarm switch (GAUGE_COUNT = none)
static bool         alarm_auto_switched  = false;       // True if current gauge was set by alarm
static uint32_t     alarm_cleared_time   = 0;           // Timestamp when alarm condition first cleared

// Thread-safe pending switch request (Driver_Loop sets, main loop handles)
static volatile gauge_type_t pending_switch = GAUGE_COUNT;  // GAUGE_COUNT = no pending switch

// TPMS audio alert one-shot flag
static bool tpms_mp3_played = false;

// TPMS battery alarm: check on ignition ON transition, 1-hour cooldown
#define TPMS_BATT_RED_THRESHOLD   2      // Battery % below this = red zone
#define TPMS_BATT_CHECK_DELAY_MS  15000  // Wait 15s after ignition ON for BLE data
#define TPMS_BATT_COOLDOWN_MS     (60UL * 60 * 1000)  // 1 hour cooldown
static bool     tpms_batt_check_pending = false;  // True after ignition ON transition
static uint32_t tpms_batt_check_start   = 0;      // When the pending check was armed
static uint32_t tpms_batt_last_alarm_ms = 0;      // Timestamp of last battery alarm (0 = never)

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

// Coolant temperature sender on ADS1115 AIN1
// Land Rover NTC sender via high-impedance conditioning circuit:
//   TEMP_SIG → BAV99 clamp → R25(1M) to 3.3V / R27(330K) to GND / C1(100n) → AIN1
// Linear approximation:  T ≈ 50 + (1.88 − V) / (1.88 − 0.68) × 70
//   0.68V → 120°C,  1.88V → 50°C
#define TEMP_ADC_CHANNEL        1       // AIN1
#define TEMP_V_COLD             1.88f   // Voltage at cold end (50°C)
#define TEMP_V_HOT              0.68f   // Voltage at hot end (120°C)
#define TEMP_C_COLD             50.0f   // Temperature at cold end
#define TEMP_C_HOT              120.0f  // Temperature at hot end
static bool temp_adc_configured = false;

/**
 * @brief Convert AIN1 voltage to temperature (linear).
 *        T = 50 + (1.88 − V) / 1.2 × 70
 */
static float coolant_voltage_to_temp(float v)
{
    float t = TEMP_C_COLD
            + (TEMP_V_COLD - v) / (TEMP_V_COLD - TEMP_V_HOT)
            * (TEMP_C_HOT - TEMP_C_COLD);
    return t;
}

// Ignition state tracking
static bool ignition_on = true;        // Assume ON at boot (display starts active)
static bool system_sleeping = false;   // True when in low-power standby mode
#define FORCE_IGNITION_ON  0           // TEMP: set to 1 to bypass ignition detection

/**
 * Expansion board power mode (compile-time configuration):
 *
 *  EXBD_POWER_ALWAYS_ON  (0) — Board is permanently powered (has its own
 *                               regulator).  Ignition state is read from
 *                               MCP23017 IO1 (EXBD_INPUT_IGNITION).
 *
 *  EXBD_POWER_IGN_SWITCHED (1) — Board is powered through the ignition
 *                               circuit.  Ignition state is inferred from
 *                               I2C bus presence: devices responding =
 *                               ignition ON, bus dead = ignition OFF.
 *                               Lower quiescent current but needs TCA4307
 *                               I2C buffer to protect the Waveshare bus.
 */
#define EXBD_POWER_ALWAYS_ON     0
#define EXBD_POWER_IGN_SWITCHED  1
#define EXBD_POWER_MODE          EXBD_POWER_ALWAYS_ON     // <-- change here

// Temporary wake: select button or screen touch wakes display for 5 min with ignition off
#define TEMP_WAKE_DURATION_MS  (5 * 60 * 1000)  // 5 minutes
static bool     temp_wake_active = false;   // Currently in temporary wake mode
static uint32_t temp_wake_start  = 0;       // Timestamp when temp wake began

// Forward declarations
void switch_gauge(gauge_type_t new_gauge);
void set_all_gauges_night_mode(bool night_mode);
static void touch_event_handler(lv_event_t * e);
static void handle_select_action(void);

// Forward declaration for input handling
static void handle_next_gauge_input(void);
static void handle_prev_gauge_input(void);
static void enter_standby_mode(void);
static void exit_standby_mode(void);

/*******************************************************************************
 * Auto-switch alarm system
 *
 * Table-driven alarm checks.  Each entry has a check function that returns
 * true when the alarm condition is active, and a target gauge to switch to.
 * Entries are evaluated in priority order — first active alarm wins.
 * A lockout timer prevents fighting with the user after manual navigation.
 *
 * To add a new alarm:
 *   1. Write a static bool alarm_check_xxx(void) function
 *   2. Add an entry to alarm_table[] at the desired priority position
 ******************************************************************************/

typedef struct {
    bool (*check_fn)(void);           /* true = alarm condition active */
    gauge_type_t target;              /* gauge to display              */
    const char *name;                 /* for log messages              */
} alarm_entry_t;

/* ── Individual alarm check functions ──────────────────────────────────── */

/** Tilt alarm: IMU roll reaches yellow zone (30°+) */
static bool alarm_check_tilt(void)
{
    float roll = imu_get_roll();
    return fabsf(roll) >= AUTO_SWITCH_ROLL_THRESHOLD;
}

/** EGT alarm: MCP9600 hardware alert (680°C warning or 750°C danger) */
static bool alarm_check_egt(void)
{
    if (!mcp9600_is_ready()) return false;
    uint8_t status = 0;
    if (mcp9600_read_status(&status) != ESP_OK) return false;
    return (status & (MCP9600_STATUS_ALERT1 | MCP9600_STATUS_ALERT2)) != 0;
}

/** TPMS alarm: rapid pressure drop (≥5 PSI within 60 s on any tire) */
static bool alarm_check_tpms_drop(void)
{
    if (!ble_tpms_any_sensor_present()) return false;
    return ble_tpms_check_pressure_drop_alarm();
}

/** TPMS alarm: any connected sensor below 15 PSI */
#define TPMS_LOW_PRESSURE_PSI  15.0f
#define TPMS_LOW_PRESSURE_BAR  (TPMS_LOW_PRESSURE_PSI / 14.5038f)
static bool alarm_check_tpms_low(void)
{
    if (!ble_tpms_any_sensor_present()) return false;
    return ble_tpms_any_low_pressure(TPMS_LOW_PRESSURE_BAR);
}

/** Cooling alarm: any fan or coolant low signal active */
static bool alarm_check_cooling(void)
{
    return cooling_alarm_active();
}

/* ── Alarm table — evaluated in priority order (highest first) ─────── */
static const alarm_entry_t alarm_table[] = {
    { alarm_check_tilt,      GAUGE_TILT,          "Tilt warning"       },
    { alarm_check_egt,       GAUGE_EGT,           "EGT over-temp"      },
    { alarm_check_cooling,   GAUGE_COOLING,       "Cooling alarm"      },
    { alarm_check_tpms_drop, GAUGE_TIRE_PRESSURE, "TPMS pressure drop" },
    { alarm_check_tpms_low,  GAUGE_TIRE_PRESSURE, "TPMS low pressure"  },
};
#define ALARM_TABLE_COUNT  (sizeof(alarm_table) / sizeof(alarm_table[0]))

/**
 * @brief Check all alarm conditions and auto-switch if triggered
 *
 * Called every iteration of the main LVGL loop (~10 ms).
 * Skipped during the manual-switch lockout period.
 * First active alarm (highest priority) wins; only one switch per cycle.
 */
static void check_auto_switch_alarms(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if ((now - manual_switch_time) < AUTO_SWITCH_LOCKOUT_MS) return;

    /* ── Check if we should return to previous gauge after alarm cleared ── */
    if (alarm_auto_switched) {
        /* Check if the alarm that brought us here is still active */
        bool still_active = false;
        for (int i = 0; i < (int)ALARM_TABLE_COUNT; i++) {
            if (alarm_table[i].target == current_gauge && alarm_table[i].check_fn()) {
                still_active = true;
                break;
            }
        }

        if (still_active) {
            alarm_cleared_time = 0;  /* Reset — alarm is still active */
        } else {
            if (alarm_cleared_time == 0) {
                alarm_cleared_time = now;  /* Start the 30 s countdown */
                ESP_LOGI("ALARM", "Alarm cleared — will return to gauge %d in %d s",
                         alarm_previous_gauge, ALARM_RETURN_DELAY_MS / 1000);
            } else if ((now - alarm_cleared_time) >= ALARM_RETURN_DELAY_MS) {
                /* 30 s elapsed with alarm clear — switch back */
                gauge_type_t return_to = alarm_previous_gauge;
                alarm_auto_switched = false;
                alarm_previous_gauge = GAUGE_COUNT;
                alarm_cleared_time = 0;
                tpms_mp3_played = false;  /* Reset TPMS audio one-shot */

                if (!should_skip_gauge(return_to)) {
                    ESP_LOGW("ALARM", "Alarm clear for %d s — returning to gauge %d",
                             ALARM_RETURN_DELAY_MS / 1000, return_to);
                    manual_switch_time = now;  /* lockout after return */
                    switch_gauge(return_to);
                    return;
                }
            }
        }
    }

    /* ── Scan alarm table for new alarms ──────────────────────────────── */
    for (int i = 0; i < (int)ALARM_TABLE_COUNT; i++) {
        const alarm_entry_t *a = &alarm_table[i];

        /* Skip if already on the target gauge */
        if (current_gauge == a->target) continue;

        if (a->check_fn()) {
            ESP_LOGW("ALARM", "%s — auto-switching to gauge %d", a->name, a->target);
            last_activity_time = now;
            manual_switch_time = now;   /* lockout prevents immediate re-trigger */

            /* Save the current gauge so we can return when alarm clears */
            if (!alarm_auto_switched) {
                alarm_previous_gauge = current_gauge;
            }
            alarm_auto_switched = true;
            alarm_cleared_time = 0;

            /* Clear one-shot alarm flags before switching */
            if (a->target == GAUGE_TIRE_PRESSURE) {
                ble_tpms_clear_pressure_drop_alarm();
                if (!tpms_mp3_played) {
                    tpms_mp3_played = true;
                    Play_Music("/sdcard", "tirewar.mp3");
                    ESP_LOGW("ALARM", "Playing tirewar.mp3");
                }
            }

            switch_gauge(a->target);
            return;     /* one switch per cycle */
        }
    }
}

// Recursively strip CLICKABLE from an object and all its descendants
static void strip_clickable_recursive(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    uint32_t cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        strip_clickable_recursive(lv_obj_get_child(obj, i));
    }
}

// Helper function to create the touch overlay (only called if touch_available)
// Uses a transparent full-screen object as the LAST child of the screen,
// ensuring it sits above all gauge content and captures all touch events.
static void create_touch_overlay(void)
{
    if (!touch_available) return;  // Runtime check
    
    lv_obj_t *screen = lv_scr_act();

    /* Recursively strip CLICKABLE from every existing child tree so
       nothing can steal touch events from our overlay. */
    uint32_t cnt = lv_obj_get_child_cnt(screen);
    for (uint32_t i = 0; i < cnt; i++) {
        strip_clickable_recursive(lv_obj_get_child(screen, i));
    }

    touch_overlay = lv_obj_create(screen);
    lv_obj_set_size(touch_overlay, 360, 360);
    lv_obj_set_pos(touch_overlay, 0, 0);
    lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_overlay, 0, 0);
    lv_obj_set_style_pad_all(touch_overlay, 0, 0);
    lv_obj_clear_flag(touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_overlay, touch_event_handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_overlay, touch_event_handler, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(touch_overlay, touch_event_handler, LV_EVENT_LONG_PRESSED, NULL);
}

// TPMS update callback - links BLE sensor data to tire pressure gauge
static void tpms_update_cb(tpms_position_t position, const tpms_sensor_data_t *data)
{
    // Update the tire pressure gauge with pressure, temperature, and battery
    tire_pressure_set_sensor_data((int)position, data->pressure_psi, 
                                   data->temperature_c, data->battery_percent);
}

// Select action handler — dispatches gauge-specific action (same as hardware select button)
static void handle_select_action(void)
{
    switch (current_gauge) {
        case GAUGE_COMPASS:
            ESP_LOGD("MAIN", "Select: toggle compass calibration");
            compass_toggle_calibration();
            break;
        case GAUGE_BOOST:
            ESP_LOGD("MAIN", "Select: toggle boost units");
            boost_toggle_units();
            break;
        case GAUGE_TIRE_PRESSURE:
            ESP_LOGD("MAIN", "Select: toggle tire pressure units");
            tire_pressure_toggle_units();
            break;
        case GAUGE_EGT:
            ESP_LOGD("MAIN", "Select: toggle EGT units");
            egt_toggle_units();
            break;
        case GAUGE_CLOCK:
            ESP_LOGD("MAIN", "Select: force NTP sync");
            wifi_ntp_force_start();
            break;
        case GAUGE_TILT:
            ESP_LOGD("MAIN", "Select: tilt zero-offset");
            tilt_zero_offset();
            break;
        case GAUGE_INCLINE:
            ESP_LOGD("MAIN", "Select: incline cycle mode");
            incline_cycle_mode();
            break;
        case GAUGE_COOLING:
            ESP_LOGD("MAIN", "Select: cooling (long-press for wading)");
            // Wading toggle moved to long-press — single tap does nothing on cooling
            break;
        default:
            // ESP_LOGD("MAIN", "Select action (no action on this gauge)");
            break;
    }
}

// Touch zones: left/right edges for navigation, center circle for long-press select.
// The center zone (100px radius) is excluded from left/right taps to prevent overlap.
#define TOUCH_CENTER_RADIUS  100
#define TOUCH_CENTER_R2      (TOUCH_CENTER_RADIUS * TOUCH_CENTER_RADIUS)
#define TOUCH_DEBOUNCE_MS    300   // Minimum ms between gauge switches (prevents double-fire)

static inline bool touch_in_center(const lv_point_t *p)
{
    int dx = p->x - 180;
    int dy = p->y - 180;
    return (dx * dx + dy * dy) <= TOUCH_CENTER_R2;
}

/* Capture the initial press position so left/right is determined by where
   the finger first lands, not where it drifts to before release. */
static lv_point_t touch_press_point = {0, 0};
static bool       touch_press_valid = false;
static uint32_t   last_touch_switch_ms = 0;

// Touch event handler: maps touch zones → unified input events.
// Center taps detected on PRESSED (finger-down) for reliable double-tap.
// Left/right detected on SHORT_CLICKED (finger-up) with debounce.
// Cooling gauge has special long-press zones (whole-screen), handled directly here.
static void touch_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_indev_get_point(indev, &touch_press_point);
        touch_press_valid = true;

        /* Center taps: detect on finger-down for reliable double-tap.
           The double-tap state machine handles timing; long-press cancels
           a pending tap if the finger is held. */
        if (touch_in_center(&touch_press_point)) {
            user_input_feed_select_tap();
        }
        return;
    }

    if (code == LV_EVENT_SHORT_CLICKED) {
        if (!touch_press_valid) return;
        touch_press_valid = false;

        lv_point_t point = touch_press_point;

        /* Only left/right zones here — center was handled on PRESSED */
        if (touch_in_center(&point)) return;

        /* Debounce left/right (overlay recreation can re-fire events) */
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if ((now - last_touch_switch_ms) < TOUCH_DEBOUNCE_MS) return;
        last_touch_switch_ms = now;

        if (point.x < 180) {
            user_input_feed_prev();
        } else {
            user_input_feed_next();
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        lv_point_t point = touch_press_valid ? touch_press_point : (lv_point_t){0, 0};
        if (!touch_press_valid) {
            lv_indev_t *indev = lv_indev_get_act();
            lv_indev_get_point(indev, &point);
        }
        touch_press_valid = false;

        /* Cooling gauge: three long-press zones across the whole screen */
        if (current_gauge == GAUGE_COOLING) {
            warning_beep_play(BEEP_SHORT);
            if (point.y < 180) {
                if (point.x < 180) {
                    ESP_LOGW("MAIN", "Touch long-press top-left — toggle fan low");
                    cooling_toggle_fan_low();
                } else {
                    ESP_LOGW("MAIN", "Touch long-press top-right — toggle fan high");
                    cooling_toggle_fan_high();
                }
            } else {
                ESP_LOGW("MAIN", "Touch long-press bottom — toggle wading");
                cooling_toggle_wading();
            }
        } else if (touch_in_center(&point)) {
            /* Centre long-press → cancels pending tap and emits SELECT_LONG */
            user_input_feed_select_long();
        }
    }
}

// Common input handler for both touch and button
static void handle_next_gauge_input(void)
{
    int idx = gauge_order_index(current_gauge);
    gauge_type_t next_gauge = current_gauge;

    for (int step = 0; step < (int)GAUGE_ORDER_COUNT; step++) {
        idx = (idx + 1) % (int)GAUGE_ORDER_COUNT;
        if (!should_skip_gauge(gauge_order[idx])) {
            next_gauge = gauge_order[idx];
            break;
        }
    }

    // Record manual switch time to prevent immediate auto-switch back
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    manual_switch_time = now;
    last_activity_time = now;  // Reset inactivity timer
    if (temp_wake_active) temp_wake_start = now;  // Extend temp wake on activity

    // Cancel alarm auto-return — user chose to navigate away
    alarm_auto_switched = false;
    alarm_previous_gauge = GAUGE_COUNT;
    alarm_cleared_time = 0;
    
    ESP_LOGD("MAIN", "Input - switching from gauge %d to %d (next, lockout %dms)", 
             current_gauge, next_gauge, AUTO_SWITCH_LOCKOUT_MS);
    switch_gauge(next_gauge);
}

// Common input handler for previous gauge (backward cycling)
static void handle_prev_gauge_input(void)
{
    int idx = gauge_order_index(current_gauge);
    gauge_type_t prev_gauge = current_gauge;

    for (int step = 0; step < (int)GAUGE_ORDER_COUNT; step++) {
        idx = (idx == 0) ? (int)(GAUGE_ORDER_COUNT - 1) : (idx - 1);
        if (!should_skip_gauge(gauge_order[idx])) {
            prev_gauge = gauge_order[idx];
            break;
        }
    }

    // Record manual switch time to prevent immediate auto-switch back
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    manual_switch_time = now;
    last_activity_time = now;
    if (temp_wake_active) temp_wake_start = now;  // Extend temp wake on activity

    // Cancel alarm auto-return — user chose to navigate away
    alarm_auto_switched = false;
    alarm_previous_gauge = GAUGE_COUNT;
    alarm_cleared_time = 0;

    ESP_LOGD("MAIN", "Input - switching from gauge %d to %d (prev, lockout %dms)",
             current_gauge, prev_gauge, AUTO_SWITCH_LOCKOUT_MS);
    switch_gauge(prev_gauge);
}

// ********************* Ignition Power Management *********************

/**
 * @brief Enter standby mode when ignition is turned off
 *
 * Turns off the display and backlight to minimize power consumption.
 * The expansion board polling task continues running at its normal rate
 * so it can detect when ignition comes back on. BLE scanning is stopped.
 */
static void enter_standby_mode(void)
{
    if (system_sleeping) return;  // Already sleeping
    
    ESP_LOGW("MAIN", "IGNITION OFF — entering standby mode");
    system_sleeping = true;
    
    // 1. Clean up the current gauge display
    switch (current_gauge) {
        case GAUGE_CLOCK:         clock_cleanup(); break;
        case GAUGE_HORIZON:       artificial_horizon_cleanup(); break;
        case GAUGE_TILT:          tilt_cleanup(); break;
        case GAUGE_INCLINE:       incline_cleanup(); break;
        case GAUGE_TIRE_PRESSURE: tire_pressure_cleanup(); ble_tpms_set_fast_scan(false); break;
        case GAUGE_BOOST:         boost_cleanup(); break;
        case GAUGE_COMPASS:       compass_cleanup(); break;
        case GAUGE_EGT:           egt_cleanup(); break;
        case GAUGE_COOLING:       cooling_cleanup(); break;
        default: break;
    }
    
    // 2. Clear the screen
    lv_obj_clean(lv_scr_act());
    touch_overlay = NULL;
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    lv_obj_invalidate(lv_scr_act());
    lv_timer_handler();  // Flush the black screen before turning off
    
    // 3. Turn off backlight and display panel
    Set_Backlight(0);
    esp_lcd_panel_disp_on_off(panel_handle, false);
    
    // 4. Stop BLE scanning to save radio power
    ble_tpms_stop_scan();
    
    // 5. Stop WiFi to save radio power
    wifi_ntp_stop();
    
    // 6. Reduce CPU frequency to 80 MHz to save power
    rtc_cpu_freq_config_t freq_config;
    if (rtc_clk_cpu_freq_mhz_to_config(80, &freq_config)) {
        rtc_clk_cpu_freq_set_config(&freq_config);
        ESP_LOGW("MAIN", "CPU frequency reduced to 80 MHz");
    }
    
    ESP_LOGW("MAIN", "Standby mode active — display off, WiFi+BLE stopped, CPU 80MHz");
}

/**
 * @brief Exit standby mode when ignition is turned on
 *
 * Re-enables the display, turns on the backlight, shows the clock gauge,
 * and resumes BLE scanning.
 */
static void exit_standby_mode(void)
{
    if (!system_sleeping) return;  // Already awake
    
    ESP_LOGW("MAIN", "IGNITION ON — waking up from standby");
    
    // 1. Restore CPU frequency to 160 MHz
    rtc_cpu_freq_config_t freq_config;
    if (rtc_clk_cpu_freq_mhz_to_config(160, &freq_config)) {
        rtc_clk_cpu_freq_set_config(&freq_config);
        ESP_LOGW("MAIN", "CPU frequency restored to 160 MHz");
    }
    
    // 2. Turn on display panel and backlight
    esp_lcd_panel_disp_on_off(panel_handle, true);
    Set_Backlight(test_night_mode ? BACKLIGHT_NIGHT : BACKLIGHT_DAY);
    
    // 3. Show the clock gauge (always start with clock after wake)
    current_gauge = GAUGE_CLOCK;
    clock_init();
    clock_set_night_mode(test_night_mode);
    clock_set_visible(true);
    
    // Recreate touch overlay if touch is available
    if (touch_available) {
        create_touch_overlay();
    }
    
    // WiFi NTP sync is now triggered manually via long-press on clock gauge
    
    // 5. Resume BLE scanning
    ble_tpms_start_scan();
    
    // 6. Reset ADC config flags (will reconfigure when gauge shown)
    map_adc_configured = false;
    temp_adc_configured = false;
    
    // 7. Reset activity timestamp
    last_activity_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 8. Arm TPMS battery check (delayed — need BLE data first)
    tpms_batt_check_pending = true;
    tpms_batt_check_start = last_activity_time;
    
    system_sleeping = false;
    ESP_LOGW("MAIN", "System active — clock displayed, WiFi+BLE resumed");
}

void switch_gauge(gauge_type_t new_gauge)
{
    ESP_LOGD("MAIN", "switch_gauge called: current=%d, new=%d, night_mode=%d", 
             current_gauge, new_gauge, test_night_mode);

    /* Block switching away from cooling gauge while wading mode is active */
    if (current_gauge == GAUGE_COOLING && new_gauge != GAUGE_COOLING && cooling_get_wading()) {
        ESP_LOGW("MAIN", "Wading mode active — cannot leave cooling gauge");
        return;
    }
    
    if (new_gauge == current_gauge) {
        return;
    }

    user_input_reset();  /* Cancel pending double-taps etc. */
    
    /* Set input mode: DIRECT for settings (no double-tap), NORMAL for gauges */
    user_input_set_mode(new_gauge == GAUGE_SETTINGS ?
                        INPUT_MODE_DIRECT : INPUT_MODE_NORMAL);
    
    // Hide/cleanup current gauge
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGD("MAIN", "Cleaning up clock");
            clock_cleanup();
            break;
        case GAUGE_HORIZON:
            ESP_LOGD("MAIN", "Cleaning up horizon");
            artificial_horizon_cleanup();
            break;
        case GAUGE_TILT:
            ESP_LOGD("MAIN", "Cleaning up tilt");
            tilt_cleanup();
            break;
        case GAUGE_INCLINE:
            ESP_LOGD("MAIN", "Cleaning up incline");
            incline_cleanup();
            break;
        case GAUGE_TIRE_PRESSURE:
            ESP_LOGD("MAIN", "Cleaning up tire pressure");
            tire_pressure_cleanup();
            ble_tpms_set_fast_scan(false);  // Disable fast scan when leaving TPMS gauge
            break;
        case GAUGE_BOOST:
            ESP_LOGD("MAIN", "Cleaning up boost gauge");
            boost_cleanup();
            break;
        case GAUGE_COMPASS:
            ESP_LOGD("MAIN", "Cleaning up compass gauge");
            compass_cleanup();
            break;
        case GAUGE_EGT:
            ESP_LOGD("MAIN", "Cleaning up EGT gauge");
            egt_cleanup();
            break;
        case GAUGE_COOLING:
            ESP_LOGD("MAIN", "Cleaning up cooling gauge");
            cooling_cleanup();
            break;
        case GAUGE_SETTINGS:
            ESP_LOGD("MAIN", "Cleaning up settings screen");
            settings_screen_cleanup();
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
    ESP_LOGD("MAIN", "Screen cleared");
    
    // Initialize and show new gauge with current night mode setting
    current_gauge = new_gauge;
    switch (current_gauge) {
        case GAUGE_CLOCK:
            ESP_LOGD("MAIN", "Initializing clock (%s mode)", test_night_mode ? "night" : "day");
            clock_init();
            clock_set_night_mode(test_night_mode);
            break;
        case GAUGE_HORIZON:
            ESP_LOGD("MAIN", "Initializing horizon (%s mode)", test_night_mode ? "night" : "day");
            artificial_horizon_init();
            artificial_horizon_set_night_mode(test_night_mode);
            break;
        case GAUGE_TILT:
            ESP_LOGD("MAIN", "Initializing tilt (%s mode)", test_night_mode ? "night" : "day");
            tilt_init();
            tilt_set_night_mode(test_night_mode);
            break;
        case GAUGE_INCLINE:
            ESP_LOGD("MAIN", "Initializing incline (%s mode)", test_night_mode ? "night" : "day");
            incline_init();
            incline_set_night_mode(test_night_mode);
            break;
        case GAUGE_TIRE_PRESSURE:
            ESP_LOGD("MAIN", "Initializing tire pressure (%s mode)", test_night_mode ? "night" : "day");
            tire_pressure_init();
            tire_pressure_set_night_mode(test_night_mode);
            ble_tpms_set_fast_scan(true);  // Enable fast scan when viewing TPMS gauge
            break;
        case GAUGE_BOOST:
            ESP_LOGD("MAIN", "Initializing boost gauge (%s mode)", test_night_mode ? "night" : "day");
            boost_init();
            boost_set_night_mode(test_night_mode);
            break;
        case GAUGE_COMPASS:
            ESP_LOGD("MAIN", "Initializing compass gauge (%s mode)", test_night_mode ? "night" : "day");
            compass_init();
            compass_set_night_mode(test_night_mode);
            break;
        case GAUGE_EGT:
            ESP_LOGD("MAIN", "Initializing EGT gauge (%s mode)", test_night_mode ? "night" : "day");
            egt_init();
            egt_set_night_mode(test_night_mode);
            break;
        case GAUGE_COOLING:
            ESP_LOGD("MAIN", "Initializing cooling gauge (%s mode)", test_night_mode ? "night" : "day");
            cooling_init();
            cooling_set_night_mode(test_night_mode);
            break;
        case GAUGE_SETTINGS:
            ESP_LOGD("MAIN", "Initializing settings screen (%s mode)", test_night_mode ? "night" : "day");
            settings_screen_set_night_mode(test_night_mode);
            settings_screen_init();
            break;
        default:
            break;
    }
    
    // Recreate touch overlay on top of the new gauge (if touch available)
    if (touch_available) {
        create_touch_overlay();
        // ESP_LOGD("MAIN", "Touch overlay recreated");
    }
    
    ESP_LOGI("MAIN", "Switched to %s (%s mode)", 
             current_gauge == GAUGE_CLOCK ? "Clock" :
             current_gauge == GAUGE_HORIZON ? "Horizon" :
             current_gauge == GAUGE_TILT ? "Tilt" :
             current_gauge == GAUGE_TIRE_PRESSURE ? "Tire Pressure" :
             current_gauge == GAUGE_BOOST ? "Boost" :
             current_gauge == GAUGE_COMPASS ? "Compass" :
             current_gauge == GAUGE_EGT ? "EGT" :
             current_gauge == GAUGE_COOLING ? "Cooling" :
             current_gauge == GAUGE_SETTINGS ? "Settings" : "Unknown",
             test_night_mode ? "night" : "day");
}

void Driver_Loop(void *parameter)
{
    while(1)
    {
        // Skip all sensor polling while in standby — just sleep
        if (system_sleeping) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        
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
        // NOTE: tilt update moved to main app_main loop for LVGL thread safety
        
        // NOTE: Auto-switch alarms are now handled in the main LVGL loop via
        //       check_auto_switch_alarms() — see alarm_table[] for the list.
        
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

    /* NVS flash must be initialised before settings_load() can read it */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_LOGI("MAIN", "NVS flash init: %s", esp_err_to_name(ret));

    settings_load();                   // Restore calibration + unit preferences from NVS
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
    
    // Suppress internal I2C driver NACK error logs — the touch controller
    // generates expected NACKs during polling when no touch is active, and
    // absent devices (MCP9600) also cause harmless NACK noise.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    
    LCD_Init();
    Audio_Init();
    warning_beep_init();  // Initialize beep system early (before any gauge)
    button_input_init();  // Initialize button input (works on all versions)
    user_input_init();    // Unified input: combo, double-tap, auto-repeat
    LVGL_Init();   // returns the screen object

    // ********************* Gauge Displays *********************
    // Initialize clock and display with current RTC time (before WiFi/NTP sync)
    clock_init();
    clock_set_night_mode(test_night_mode);  // Apply initial day/night mode
    clock_set_visible(true);  // Clock visible by default
    
    // Other gauges will be initialized on-demand when switching
    
    // Initialize WiFi NTP (registers handlers, stops WiFi until needed)
    // Sync is triggered manually via long-press on clock gauge
    wifi_ntp_init();
    
    // Initialize BLE TPMS (using NimBLE - lighter weight than Bluedroid)
    if (ble_tpms_init() == ESP_OK) {
        ESP_LOGI("MAIN", "BLE TPMS initialized (NimBLE)");
        
        // Register TPMS sensors: use NVS-saved MACs if available, otherwise hardcoded defaults
        static const char *default_macs[TPMS_POSITION_COUNT] = {
            "80:EA:CA:50:3A:51",  // FL
            "81:EA:CA:50:3B:6B",  // FR
            "82:EA:CA:50:3B:13",  // RL
            "83:EA:CA:50:3B:6C",  // RR
        };
        for (int i = 0; i < TPMS_POSITION_COUNT; i++) {
            uint8_t mac[6];
            if (settings_get_tpms_mac((tpms_position_t)i, mac)) {
                ble_tpms_register_sensor((tpms_position_t)i, mac);
                ESP_LOGI("MAIN", "TPMS %s: NVS MAC %02X:%02X:%02X:%02X:%02X:%02X",
                         i==0?"FL":i==1?"FR":i==2?"RL":"RR",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            } else {
                ble_tpms_register_sensor_str((tpms_position_t)i, default_macs[i]);
                ESP_LOGI("MAIN", "TPMS %s: default MAC %s", 
                         i==0?"FL":i==1?"FR":i==2?"RL":"RR", default_macs[i]);
            }
        }
        
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

    // Check initial ignition state — expansion board presence = ignition ON
    // (The expansion board is powered by the ignition circuit, so if I2C devices
    // respond, ignition is on. If they don't respond, ignition is off.)
    if (!FORCE_IGNITION_ON) {
#if EXBD_POWER_MODE == EXBD_POWER_ALWAYS_ON
        // Permanently powered board — read ignition signal from MCP23017 IO1
        if (exbd_has_io()) {
            ignition_on = exbd_get_input(EXBD_INPUT_IGNITION);
            if (!ignition_on) {
                ESP_LOGW("MAIN", "Ignition OFF at boot — entering standby immediately");
                enter_standby_mode();
            } else {
                ESP_LOGI("MAIN", "Ignition ON at boot — normal operation");
            }
        } else {
            ESP_LOGI("MAIN", "No I/O expander — skipping ignition check");
        }
#else
        // Ignition-switched board — presence on I2C = ignition ON
        ignition_on = expansion_board_detected();
        if (!ignition_on) {
            ESP_LOGW("MAIN", "Expansion board not detected at boot — entering standby");
            enter_standby_mode();
        } else {
            ESP_LOGI("MAIN", "Expansion board detected at boot — ignition ON");
        }
#endif
    }

    while (1) {
        // *** Ignition power management ***
        if (!FORCE_IGNITION_ON) {
#if EXBD_POWER_MODE == EXBD_POWER_ALWAYS_ON
            // Permanently powered board — read ignition input from MCP23017
            if (exbd_has_io()) {
                bool ign_now = exbd_get_input(EXBD_INPUT_IGNITION);
                if (ign_now && !ignition_on) {
                    ignition_on = true;
                    exit_standby_mode();
                } else if (!ign_now && ignition_on) {
                    ignition_on = false;
                    enter_standby_mode();
                }
            }
#else
            // Ignition-switched board — I2C presence = ignition ON
            // The poll task auto-clears board_detected after consecutive I2C failures.
            bool board_now = expansion_board_detected();
            if (board_now && !ignition_on) {
                ignition_on = true;
                exit_standby_mode();
            } else if (!board_now && ignition_on) {
                ignition_on = false;
                enter_standby_mode();
            }
#endif
        }
        
        // While in standby, check for wake triggers
        if (system_sleeping) {
            bool wake_trigger = false;

#if EXBD_POWER_MODE == EXBD_POWER_ALWAYS_ON
            // Permanently powered board — check select button for temp-wake
            if (expansion_board_detected() && exbd_select_pressed()) {
                wake_trigger = true;
            }
#else
            // Ignition-switched board — probe I2C every ~2s to detect board return
            static uint32_t last_probe_ms = 0;
            uint32_t probe_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((probe_now - last_probe_ms) >= 2000) {
                last_probe_ms = probe_now;
                if (expansion_board_probe()) {
                    ESP_LOGW("MAIN", "Expansion board detected — ignition ON");
                    ignition_on = true;
                    exit_standby_mode();
                    continue;
                }
            }
#endif

            // Check screen touch (GPIO4 goes LOW on touch)
            if (touch_available && gpio_get_level(I2C_Touch_INT_IO) == 0) {
                wake_trigger = true;
            }

            // Check any physical button press (wake without expansion board or touch)
            if (button_input_both_held()) {
                wake_trigger = true;
            }

            if (wake_trigger) {
                ESP_LOGW("MAIN", "Standby wake — display active for %d min",
                         TEMP_WAKE_DURATION_MS / 60000);
                temp_wake_active = true;
                temp_wake_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
                exit_standby_mode();
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
                lv_timer_handler();  // Keep LVGL tick alive (minimal)
                continue;
            }
        }

        // Check temp-wake timeout — go back to standby if ignition still off
        if (temp_wake_active) {
            uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - temp_wake_start;
            if (ignition_on) {
                // Ignition came on during temp wake — cancel temp wake, stay active
                temp_wake_active = false;
                ESP_LOGW("MAIN", "Ignition ON during temp wake — staying active");
            } else if (elapsed >= TEMP_WAKE_DURATION_MS) {
                ESP_LOGW("MAIN", "Temp wake expired (%d min) — returning to standby",
                         TEMP_WAKE_DURATION_MS / 60000);
                temp_wake_active = false;
                enter_standby_mode();
                continue;
            }
        }
        
        // Periodic NTP auto-sync (once per day, home network)
        // wifi_ntp_start() respects the 24-hour NVS cooldown internally,
        // so calling it every loop iteration is safe and cheap.
        if (!system_sleeping) {
            wifi_ntp_start();
        }

        // *** Unified input: buttons, expansion board, touch → event dispatch ***
        {
            input_event_t evt;
            while ((evt = user_input_poll()) != INPUT_NONE) {
                uint32_t inp_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
                last_activity_time = inp_now;
                if (temp_wake_active) temp_wake_start = inp_now;

                if (current_gauge == GAUGE_SETTINGS) {
                    /* ── Settings screen dispatch ────────────────────── */
                    bool editing = settings_screen_editing();
                    switch (evt) {
                    case INPUT_NEXT:
                        if (editing) settings_screen_char_change(+1);
                        else settings_screen_navigate(+1);
                        break;
                    case INPUT_PREV:
                        if (editing) settings_screen_char_change(-1);
                        else settings_screen_navigate(-1);
                        break;
                    case INPUT_SELECT:
                        if (editing) settings_screen_navigate(+1);  /* advance cursor */
                        else {
                            settings_screen_select();
                            if (settings_screen_wants_exit()) switch_gauge(GAUGE_CLOCK);
                        }
                        break;
                    case INPUT_SELECT_LONG:
                        settings_screen_select();  /* confirm / done */
                        if (settings_screen_wants_exit()) switch_gauge(GAUGE_CLOCK);
                        break;
                    default:
                        break;
                    }
                } else {
                    /* ── Normal gauge dispatch ───────────────────────── */
                    switch (evt) {
                    case INPUT_NEXT:
                        handle_next_gauge_input();
                        break;
                    case INPUT_PREV:
                        handle_prev_gauge_input();
                        break;
                    case INPUT_SELECT:
                        /* Single tap — no action for normal gauges */
                        break;
                    case INPUT_SELECT_DOUBLE:
                        if (current_gauge == GAUGE_CLOCK) {
                            ESP_LOGW("MAIN", "Double-tap on clock — entering settings");
                            switch_gauge(GAUGE_SETTINGS);
                        } else {
                            ESP_LOGW("MAIN", "Double-tap select — jumping to clock");
                            manual_switch_time = inp_now;
                            alarm_auto_switched = false;
                            switch_gauge(GAUGE_CLOCK);
                        }
                        break;
                    case INPUT_SELECT_LONG:
                        if (current_gauge == GAUGE_COOLING) {
                            cooling_toggle_wading();
                        } else {
                            handle_select_action();
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        
        // Night mode handling:
        // Activate night mode only when sidelights AND (dip OR full beam) are on.
        // This prevents night mode triggering from a momentary headlight flash
        // (full beam without sidelights) or parking lights alone.
        if (expansion_board_detected()) {
            bool sidelights = exbd_get_input(EXBD_INPUT_LIGHTS);
            bool headlights = exbd_get_input(EXBD_INPUT_LOW_BEAM) || exbd_get_input(EXBD_INPUT_FULL_BEAM);
            bool lights_on  = sidelights && headlights;
            if (lights_on != test_night_mode) {
                test_night_mode = lights_on;
                Set_Backlight(lights_on ? BACKLIGHT_NIGHT : BACKLIGHT_DAY);
                ESP_LOGI("MAIN", "Night mode → %s (side=%d dip=%d full=%d)",
                         lights_on ? "ON" : "OFF",
                         sidelights,
                         exbd_get_input(EXBD_INPUT_LOW_BEAM),
                         exbd_get_input(EXBD_INPUT_FULL_BEAM));
                switch (current_gauge) {
                    case GAUGE_CLOCK:         clock_set_night_mode(test_night_mode); break;
                    case GAUGE_HORIZON:       artificial_horizon_set_night_mode(test_night_mode); break;
                    case GAUGE_TILT:          tilt_set_night_mode(test_night_mode); break;
                    case GAUGE_INCLINE:       incline_set_night_mode(test_night_mode); break;
                    case GAUGE_TIRE_PRESSURE: tire_pressure_set_night_mode(test_night_mode); break;
                    case GAUGE_BOOST:         boost_set_night_mode(test_night_mode); break;
                    case GAUGE_COMPASS:       compass_set_night_mode(test_night_mode); break;
                    case GAUGE_EGT:           egt_set_night_mode(test_night_mode); break;
                    case GAUGE_COOLING:       cooling_set_night_mode(test_night_mode); break;
                    case GAUGE_SETTINGS:      settings_screen_set_night_mode(test_night_mode); break;
                    default: break;
                }
            }
        } else if (test_night_mode) {
            /* No expansion board — force day mode */
            test_night_mode = false;
            Set_Backlight(BACKLIGHT_DAY);
            ESP_LOGI("MAIN", "No expansion board — forcing DAY mode");
            switch (current_gauge) {
                case GAUGE_CLOCK:         clock_set_night_mode(false); break;
                case GAUGE_TILT:          tilt_set_night_mode(false); break;
                case GAUGE_INCLINE:       incline_set_night_mode(false); break;
                case GAUGE_TIRE_PRESSURE: tire_pressure_set_night_mode(false); break;
                default: break;
            }
        }
        
        // Read MAP sensor and update boost gauge (only when boost gauge is active)
        // Rate-limited to ~20 FPS to reduce needle tearing
        if (current_gauge == GAUGE_BOOST && expansion_board_detected()) {
            static uint32_t last_boost_redraw_ms = 0;
            uint32_t boost_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((boost_now - last_boost_redraw_ms) < 50) goto skip_boost;  // ~20 FPS

            // Configure ADC for fast MAP reads on first use
            if (!map_adc_configured) {
                ads1115_set_gain(ADS1115_PGA_2048);    // ±2.048V for 0-2V divider output
                ads1115_set_data_rate(ADS1115_DR_860SPS); // Fast conversion (~2ms)
                map_adc_configured = true;
                temp_adc_configured = false;  // Force coolant temp to reconfigure if shown later
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
                    ESP_LOGD("MAP", "V=%.3f kPa=%.1f BAR=%.2f", voltage, kpa_abs, boost_bar);
                }
                boost_set_value(boost_bar);
            } else {
                static int err_counter = 0;
                if ((err_counter++ % 100) == 0) {
                    ESP_LOGE("MAP", "ADC read failed: %s", esp_err_to_name(adc_ret));
                }
            }
            last_boost_redraw_ms = boost_now;
        }
        skip_boost:

        // Read compass heading and update compass gauge (only when active)
        if (current_gauge == GAUGE_COMPASS && expansion_board_detected()) {
            float heading = 0.0f;
            if (lis3mdl_get_heading(&heading) == ESP_OK) {
                compass_set_heading(heading);
            }
        }

        // Read EGT and update gauge (only when active)
        // Rate-limited to ~20 FPS to reduce needle tearing
        if (current_gauge == GAUGE_EGT && mcp9600_is_ready()) {
            static uint32_t last_egt_redraw_ms = 0;
            uint32_t egt_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((egt_now - last_egt_redraw_ms) >= 50) {  // ~20 FPS
                float egt_temp = 0.0f;
                if (mcp9600_read_temperature(&egt_temp) == ESP_OK) {
                    static int egt_log_counter = 0;
                    if ((egt_log_counter++ % 50) == 0) {
                        ESP_LOGD("EGT", "Temp=%.1f°C", egt_temp);
                    }
                    egt_set_value(egt_temp);
                }
                last_egt_redraw_ms = egt_now;
            }
        }

        // Update tilt gauge with IMU roll data (must run in LVGL task)
        // Rate-limited to ~15 FPS to keep rendering load low and touch responsive
        if (current_gauge == GAUGE_TILT) {
            static uint32_t last_tilt_redraw_ms = 0;
            uint32_t tilt_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((tilt_now - last_tilt_redraw_ms) >= 66) {  // ~15 FPS
                last_tilt_redraw_ms = tilt_now;
                tilt_set_angle(imu_get_roll());
            }
        }

        // Update incline gauge with IMU pitch data (~15 FPS)
        if (current_gauge == GAUGE_INCLINE) {
            static uint32_t last_incline_redraw_ms = 0;
            uint32_t incl_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((incl_now - last_incline_redraw_ms) >= 66) {  // ~15 FPS
                last_incline_redraw_ms = incl_now;
                incline_set_angle(imu_get_pitch());
            }
        }

        // Update cooling gauge with expansion board input states (~10 Hz)
        if (current_gauge == GAUGE_COOLING && expansion_board_detected()) {
            static uint32_t last_cooling_ms = 0;
            uint32_t cool_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((cool_now - last_cooling_ms) >= 100) {  // ~10 Hz
                last_cooling_ms = cool_now;
                cooling_update();
            }
        }

        // Update settings screen (TPMS learn scanning animation etc.)
        if (current_gauge == GAUGE_SETTINGS) {
            settings_screen_update();
        }

        // Read coolant temperature from ADS1115 AIN1 (~2 Hz, always when not on boost)
        // Runs even when not on cooling gauge so overtemp alarm can trigger auto-switch.
        // Skipped while boost gauge is active (different ADC gain).
        if (expansion_board_detected() && current_gauge != GAUGE_BOOST) {
            static uint32_t last_temp_ms = 0;
            uint32_t temp_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((temp_now - last_temp_ms) >= 500) {  // ~2 Hz — temp changes slowly
                last_temp_ms = temp_now;
                if (!temp_adc_configured) {
                    ads1115_set_gain(ADS1115_PGA_4096);      // ±4.096V for 0-3.3V sender range
                    ads1115_set_data_rate(ADS1115_DR_128SPS); // 128 SPS — plenty for slow temp
                    temp_adc_configured = true;
                    map_adc_configured = false;  // Force boost to reconfigure if shown later
                    ESP_LOGI("MAIN", "Coolant temp ADC configured: ±4.096V, 128 SPS");
                }
                float voltage = 0.0f;
                if (ads1115_read_single(TEMP_ADC_CHANNEL, &voltage) == ESP_OK) {
                    float temp_c = coolant_voltage_to_temp(voltage);
                    static int temp_log_ctr = 0;
                    if ((temp_log_ctr++ % 10) == 0) {  // Log every ~5s
                        ESP_LOGI("COOLANT", "AIN1=%.3fV → %.1f°C", voltage, temp_c);
                    }
                    cooling_set_coolant_temp(temp_c);
                }
            }
        }

        // *** Auto-switch alarm system — checks tilt, EGT, TPMS in priority order ***
        check_auto_switch_alarms();

        // *** Inactivity timeout — return to Clock after 10 minutes of no interaction ***
        if (ignition_on && !system_sleeping && current_gauge != GAUGE_CLOCK && !alarm_auto_switched) {
            uint32_t inact_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((inact_now - last_activity_time) >= INACTIVITY_TIMEOUT_MS) {
                ESP_LOGW("MAIN", "Inactivity timeout (%d min) — returning to Clock",
                         INACTIVITY_TIMEOUT_MS / 60000);
                last_activity_time = inact_now;  // Reset so it doesn't re-trigger
                manual_switch_time = inact_now;
                switch_gauge(GAUGE_CLOCK);
            }
        }

        // *** TPMS battery check — runs once after ignition ON, with 1-hour cooldown ***
        if (tpms_batt_check_pending) {
            uint32_t batt_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((batt_now - tpms_batt_check_start) >= TPMS_BATT_CHECK_DELAY_MS) {
                tpms_batt_check_pending = false;  // One-shot: don't check again
                if (ble_tpms_any_sensor_present() &&
                    ble_tpms_any_low_battery(TPMS_BATT_RED_THRESHOLD)) {
                    // Check 1-hour cooldown
                    if (tpms_batt_last_alarm_ms == 0 ||
                        (batt_now - tpms_batt_last_alarm_ms) >= TPMS_BATT_COOLDOWN_MS) {
                        tpms_batt_last_alarm_ms = batt_now;
                        ESP_LOGW("ALARM", "TPMS battery low — switching to tire pressure gauge");
                        Play_Music("/sdcard", "tirebat.mp3");
                        // Use alarm auto-switch mechanism for normal handling/timeouts
                        if (!alarm_auto_switched) {
                            alarm_previous_gauge = current_gauge;
                        }
                        alarm_auto_switched = true;
                        alarm_cleared_time = 0;
                        manual_switch_time = batt_now;
                        last_activity_time = batt_now;
                        switch_gauge(GAUGE_TIRE_PRESSURE);
                    } else {
                        ESP_LOGI("ALARM", "TPMS battery low — cooldown active, skipping");
                    }
                }
            }
        }

        // Handle pending auto-switch requests from Driver_Loop (thread-safe)
        if (pending_switch != GAUGE_COUNT) {
            gauge_type_t target = pending_switch;
            pending_switch = GAUGE_COUNT;  // Clear before switching
            ESP_LOGD("MAIN", "Processing pending auto-switch to gauge %d", target);
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






