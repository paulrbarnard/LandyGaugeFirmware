/**
 * @file boost.c
 * @brief Boost gauge display implementation using LVGL
 * 
 * Visual style matches the analog clock gauge.
 * Range: 0-2.0 Bar, warning color above 1.5 Bar.
 */

#include "boost.h"
#include "esp_log.h"
#include "LVGL_Driver/style.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "BOOST";

// Display mode
static bool night_mode = true;

// Pressure units - false = PSI, true = Bar
static bool use_bar_units = true;

// Current boost value (in PSI, converted for display if needed)
static float current_boost_psi = 0.0f;

// Conversion factors
#define PSI_TO_BAR 0.0689476f
#define BAR_TO_PSI 14.5038f

// Gauge configuration
#define GAUGE_SIZE 360
#define GAUGE_CENTER_X (GAUGE_SIZE / 2)
#define GAUGE_CENTER_Y (GAUGE_SIZE / 2)

// Boost gauge specific settings
#define BOOST_MIN_PSI 0.0f
#define BOOST_MAX_PSI 2.0f
#define BOOST_WARNING_PSI 1.5f

// Gauge arc configuration (sweep from bottom-left to bottom-right, ~270 degrees)
#define GAUGE_START_ANGLE -225.0f   // 7 o'clock position (start)
#define GAUGE_END_ANGLE 45.0f       // 5 o'clock position (end)
#define GAUGE_SWEEP (GAUGE_END_ANGLE - GAUGE_START_ANGLE)  // 270 degrees

// Tick and layout dimensions
#define TICK_OFFSET 20
#define MAJOR_TICK_OUTER_R 158      // Outer radius for major ticks
#define MAJOR_TICK_INNER_R 135      // Inner radius for major ticks
#define MINOR_TICK_OUTER_R 158      // Outer radius for minor ticks
#define MINOR_TICK_INNER_R 145      // Inner radius for minor ticks
#define NUMBER_RADIUS 110           // Radius for numbers
#define NEEDLE_LENGTH 120           // Needle length
#define CENTER_DOT_SIZE 50          // Center cap size

// Warning color (red/orange)
#define COLOR_WARNING lv_color_make(255, 60, 0)

// LVGL objects
static lv_obj_t *gauge_container = NULL;
static lv_obj_t *needle = NULL;
static lv_obj_t *units_label = NULL;
static lv_point_t needle_points[2];

/**
 * @brief Convert PSI value to gauge angle
 */
static float psi_to_angle(float psi)
{
    // Clamp to valid range
    if (psi < BOOST_MIN_PSI) psi = BOOST_MIN_PSI;
    if (psi > BOOST_MAX_PSI) psi = BOOST_MAX_PSI;
    
    // Map PSI to angle
    float ratio = (psi - BOOST_MIN_PSI) / (BOOST_MAX_PSI - BOOST_MIN_PSI);
    return GAUGE_START_ANGLE + ratio * GAUGE_SWEEP;
}

/**
 * @brief Get the appropriate color for the current boost level
 */
static lv_color_t get_needle_color(void)
{
    if (current_boost_psi >= BOOST_WARNING_PSI) {
        return COLOR_WARNING;
    }
    return get_accent_color(night_mode);
}

/**
 * @brief Update the needle position
 */
static void update_needle(void)
{
    if (!needle) return;
    
    float angle_deg = psi_to_angle(current_boost_psi);
    float angle_rad = angle_deg * M_PI / 180.0f;
    
    int center_x = GAUGE_CENTER_X - TICK_OFFSET;
    int center_y = GAUGE_CENTER_Y - TICK_OFFSET;
    
    needle_points[0].x = center_x;
    needle_points[0].y = center_y;
    needle_points[1].x = center_x + cosf(angle_rad) * NEEDLE_LENGTH;
    needle_points[1].y = center_y + sinf(angle_rad) * NEEDLE_LENGTH;
    
    lv_line_set_points(needle, needle_points, 2);
    lv_obj_set_style_line_color(needle, get_needle_color(), 0);
}

/**
 * @brief Update units label
 */
static void update_units_label(void)
{
    if (!units_label) return;
    lv_label_set_text(units_label, use_bar_units ? "BAR" : "PSI");
}

/**
 * @brief Draw the gauge face with tick marks and numbers
 */
static void draw_gauge_face(void)
{
    if (!gauge_container) {
        gauge_container = lv_obj_create(lv_scr_act());
        lv_obj_set_size(gauge_container, DISP_W, DISP_H);
        lv_obj_center(gauge_container);
        
        lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        
        lv_obj_set_style_bg_color(gauge_container, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(gauge_container, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(gauge_container, 1, 0);
        lv_obj_set_style_border_color(gauge_container, COLOR_BACKGROUND, 0);
        
        lv_obj_move_background(gauge_container);
        ESP_LOGI(TAG, "gauge_container created");
    } else {
        lv_obj_clean(gauge_container);
        ESP_LOGI(TAG, "gauge_container cleaned for redraw");
    }
    
    int center_x = GAUGE_CENTER_X;
    int center_y = GAUGE_CENTER_Y;
    
    lv_color_t accent = get_accent_color(night_mode);
    
    // Draw tick marks and numbers
    // Major ticks every 0.5 bar (0, 0.5, 1.0, 1.5, 2.0) with numbers
    // Minor ticks every 0.1 bar
    for (int tick = 0; tick <= 20; tick++) {
        float bar_val = tick * 0.1f;
        float angle_deg = psi_to_angle(bar_val);
        float angle_rad = angle_deg * M_PI / 180.0f;
        
        bool is_major = (tick % 5 == 0);
        
        int outer_r = is_major ? MAJOR_TICK_OUTER_R : MINOR_TICK_OUTER_R;
        int inner_r = is_major ? MAJOR_TICK_INNER_R : MINOR_TICK_INNER_R;
        int line_width = is_major ? 6 : 3;
        
        // Determine tick color (warning zone ticks in warning color)
        lv_color_t tick_color = (bar_val >= BOOST_WARNING_PSI) ? COLOR_WARNING : accent;
        
        // Create tick mark
        lv_obj_t *tick = lv_line_create(gauge_container);
        lv_point_t *tick_points = malloc(2 * sizeof(lv_point_t));
        
        tick_points[0].x = center_x + cosf(angle_rad) * outer_r - TICK_OFFSET;
        tick_points[0].y = center_y + sinf(angle_rad) * outer_r - TICK_OFFSET;
        tick_points[1].x = center_x + cosf(angle_rad) * inner_r - TICK_OFFSET;
        tick_points[1].y = center_y + sinf(angle_rad) * inner_r - TICK_OFFSET;
        
        lv_line_set_points(tick, tick_points, 2);
        lv_obj_set_style_line_width(tick, line_width, 0);
        lv_obj_set_style_line_color(tick, tick_color, 0);
        lv_obj_set_style_line_rounded(tick, false, 0);
        
        // Add numbers for major ticks
        if (is_major) {
            lv_obj_t *num_label = lv_label_create(gauge_container);
            char num_text[8];
            snprintf(num_text, sizeof(num_text), "%.1f", bar_val);
            lv_label_set_text(num_label, num_text);
            
            lv_color_t num_color = (bar_val >= BOOST_WARNING_PSI) ? COLOR_WARNING : accent;
            lv_obj_set_style_text_color(num_label, num_color, 0);
            lv_obj_set_style_text_font(num_label, &lv_font_montserrat_32, 0);
            
            int num_x = center_x + cosf(angle_rad) * NUMBER_RADIUS;
            int num_y = center_y + sinf(angle_rad) * NUMBER_RADIUS;
            lv_obj_align(num_label, LV_ALIGN_CENTER,
                         num_x - center_x,
                         num_y - center_y);
        }
    }
    
    // Create units label at bottom ("PSI" or "BAR")
    units_label = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(units_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(units_label, accent, 0);
    lv_obj_align(units_label, LV_ALIGN_CENTER, 0, 45);
    update_units_label();
    
    // Create "BOOST" label below units
    lv_obj_t *boost_label = lv_label_create(gauge_container);
    lv_label_set_text(boost_label, "BOOST");
    lv_obj_set_style_text_font(boost_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(boost_label, accent, 0);
    lv_obj_align(boost_label, LV_ALIGN_CENTER, 0, 65);
    
    // Create recessed shadow effects (same as clock)
    create_gauge_shadows(gauge_container, night_mode);
    
    ESP_LOGI(TAG, "Gauge face drawn");
}

/**
 * @brief Create the needle and center cap
 */
static void create_needle(void)
{
    // Create needle
    needle = lv_line_create(gauge_container);
    lv_obj_set_style_line_width(needle, 8, 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
    update_needle();
    
    // Create center cap (raised button appearance)
    lv_obj_t *center = lv_obj_create(gauge_container);
    lv_obj_set_size(center, CENTER_DOT_SIZE, CENTER_DOT_SIZE);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    
    // Gradient effect - dark grey to black
    lv_obj_set_style_bg_color(center, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_grad_color(center, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_grad_dir(center, LV_GRAD_DIR_HOR, 0);
    
    // Subtle highlight shadow
    lv_obj_set_style_shadow_width(center, 10, 0);
    lv_obj_set_style_shadow_opa(center, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(center, get_accent_color(night_mode), 0);
    lv_obj_set_style_shadow_ofs_x(center, -3, 0);
    lv_obj_set_style_shadow_ofs_y(center, -3, 0);
    
    lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
    
    ESP_LOGI(TAG, "Needle and center cap created");
}

void boost_init(void)
{
    ESP_LOGI(TAG, "Initializing boost gauge");
    
    current_boost_psi = 0.0f;
    
    draw_gauge_face();
    create_needle();
    
    ESP_LOGI(TAG, "Boost gauge initialized");
}

void boost_set_value(float psi)
{
    // Clamp to valid range
    if (psi < BOOST_MIN_PSI) psi = BOOST_MIN_PSI;
    if (psi > BOOST_MAX_PSI) psi = BOOST_MAX_PSI;
    
    current_boost_psi = psi;
    update_needle();
}

float boost_get_value(void)
{
    return current_boost_psi;
}

void boost_set_night_mode(bool is_night_mode)
{
    if (night_mode == is_night_mode) return;
    
    night_mode = is_night_mode;
    ESP_LOGI(TAG, "Setting %s mode", night_mode ? "night" : "day");
    
    // Redraw entire gauge
    draw_gauge_face();
    create_needle();
}

void boost_set_visible(bool visible)
{
    if (gauge_container) {
        if (visible) {
            lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void boost_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up boost gauge");
    
    if (gauge_container) {
        lv_obj_del(gauge_container);
        gauge_container = NULL;
        needle = NULL;
        units_label = NULL;
    }
    
    ESP_LOGI(TAG, "Boost gauge cleanup complete");
}

void boost_set_units_bar(bool use_bar)
{
    if (use_bar_units == use_bar) return;
    
    use_bar_units = use_bar;
    update_units_label();
    ESP_LOGI(TAG, "Units set to %s", use_bar_units ? "bar" : "psi");
}

void boost_toggle_units(void)
{
    use_bar_units = !use_bar_units;
    update_units_label();
    ESP_LOGI(TAG, "Units toggled to %s", use_bar_units ? "bar" : "psi");
}
