/**
 * @file tire_pressure.c
 * @brief Tire Pressure gauge display implementation using LVGL
 */

#include "tire_pressure.h"
#include "esp_log.h"
#include "LVGL_Driver/style.h"
#include <stdio.h>

static const char *TAG = "TIRE_PRESSURE";

// Display mode - can be changed at runtime
static bool night_mode = true;

// Pressure units - false = PSI, true = Bar
static bool use_bar_units = false;

// Pressure values in PSI (stored internally in PSI, converted for display if needed)
static float pressure_values[4] = {30.0f, 30.0f, 30.0f, 30.0f};  // FL, FR, RL, RR

// Conversion factor: 1 PSI = 0.0689476 Bar
#define PSI_TO_BAR 0.0689476f

// Gauge dimensions (same as other gauges)
#define GAUGE_SIZE 360

// LVGL objects
static lv_obj_t *gauge_container = NULL;
static lv_obj_t *roof_image = NULL;
static lv_obj_t *pressure_labels[4] = {NULL, NULL, NULL, NULL};  // FL, FR, RL, RR
static lv_obj_t *units_label = NULL;

// Declare the external images
LV_IMG_DECLARE(roof_110_150w);        // Day mode image
LV_IMG_DECLARE(roof_dark_110_150w);   // Night mode image

/**
 * @brief Update pressure label text based on current value and units
 */
static void update_pressure_label(int wheel)
{
    if (wheel < 0 || wheel > 3 || !pressure_labels[wheel]) return;
    
    char buf[16];
    if (use_bar_units) {
        float bar_value = pressure_values[wheel] * PSI_TO_BAR;
        snprintf(buf, sizeof(buf), "%.1f", bar_value);
    } else {
        snprintf(buf, sizeof(buf), "%.0f", pressure_values[wheel]);
    }
    lv_label_set_text(pressure_labels[wheel], buf);
}

/**
 * @brief Update all pressure labels
 */
static void update_all_pressure_labels(void)
{
    for (int i = 0; i < 4; i++) {
        update_pressure_label(i);
    }
}

/**
 * @brief Update units label text
 */
static void update_units_label(void)
{
    if (units_label) {
        lv_label_set_text(units_label, use_bar_units ? "bar" : "psi");
    }
}

/**
 * @brief Draw the tire pressure gauge face
 */
static void draw_gauge_face(void)
{
    if (!gauge_container) {
        // Create the main container
        gauge_container = lv_obj_create(lv_scr_act());
        lv_obj_set_size(gauge_container, DISP_W, DISP_H);
        lv_obj_center(gauge_container);
        
        // Disable scrolling and clipping
        lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        
        // Set dark background color (same as clock)
        lv_obj_set_style_bg_color(gauge_container, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(gauge_container, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(gauge_container, 1, 0);
        lv_obj_set_style_border_color(gauge_container, COLOR_BACKGROUND, 0);
        
        lv_obj_move_background(gauge_container);
        ESP_LOGI(TAG, "gauge_container created and centered");
    } else {
        // Clean existing children before redrawing
        lv_obj_clean(gauge_container);
        ESP_LOGI(TAG, "gauge_container children cleaned for redraw");
    }
    
    // Create the roof image centered on the gauge
    roof_image = lv_img_create(gauge_container);
    // Switch between day and night mode images
    if (night_mode) {
        lv_img_set_src(roof_image, &roof_dark_110_150w);
    } else {
        lv_img_set_src(roof_image, &roof_110_150w);
    }
    lv_obj_align(roof_image, LV_ALIGN_CENTER, 0, 0);
    
    ESP_LOGI(TAG, "roof image created and centered (%s mode)", night_mode ? "night" : "day");
    
    // Get the accent color for pressure text
    lv_color_t text_color = get_accent_color(night_mode);
    // Units label: black in day mode (roof is white), green in night mode
    lv_color_t units_color = night_mode ? COLOR_GREEN : lv_color_black();
    
    // Position offsets for pressure labels relative to center
    // Adjusted to be near each wheel position on the roof image
    // FL=front-left, FR=front-right, RL=rear-left, RR=rear-right
    const int x_left = 80;     // Horizontal distance from center for left labels
    const int x_right = 95;    // Horizontal distance from center for right labels (further out)
    const int y_front = -90;   // Vertical offset for front wheels (moved up a touch)
    const int y_rear = 100;    // Vertical offset for rear wheels (moved up a touch)
    
    // Create front-left pressure label (right-aligned so LSB is near image)
    pressure_labels[0] = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(pressure_labels[0], &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(pressure_labels[0], text_color, 0);
    lv_obj_set_style_text_align(pressure_labels[0], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(pressure_labels[0], 80);  // Fixed width for alignment
    lv_obj_align(pressure_labels[0], LV_ALIGN_CENTER, -x_left - 35, y_front);
    
    // Create front-right pressure label (left-aligned so MSB is near image)
    pressure_labels[1] = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(pressure_labels[1], &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(pressure_labels[1], text_color, 0);
    lv_obj_set_style_text_align(pressure_labels[1], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(pressure_labels[1], LV_ALIGN_CENTER, x_right, y_front);
    
    // Create rear-left pressure label (right-aligned so LSB is near image)
    pressure_labels[2] = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(pressure_labels[2], &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(pressure_labels[2], text_color, 0);
    lv_obj_set_style_text_align(pressure_labels[2], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(pressure_labels[2], 80);  // Fixed width for alignment
    lv_obj_align(pressure_labels[2], LV_ALIGN_CENTER, -x_left - 35, y_rear);
    
    // Create rear-right pressure label (left-aligned so MSB is near image)
    pressure_labels[3] = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(pressure_labels[3], &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(pressure_labels[3], text_color, 0);
    lv_obj_set_style_text_align(pressure_labels[3], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(pressure_labels[3], LV_ALIGN_CENTER, x_right, y_rear);
    
    // Create units label at center (on the roof image - black for day, green for night)
    units_label = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(units_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(units_label, units_color, 0);
    lv_obj_align(units_label, LV_ALIGN_CENTER, 0, 0);
    
    // Update all labels with current values
    update_all_pressure_labels();
    update_units_label();
    
    ESP_LOGI(TAG, "Pressure labels created");
    
    // Create recessed shadow effects (same as clock)
    create_gauge_shadows(gauge_container, night_mode);
    ESP_LOGI(TAG, "Shadow effects created");
}

void tire_pressure_init(void)
{
    ESP_LOGI(TAG, "Initializing tire pressure gauge");
    
    // Draw the gauge face with image
    draw_gauge_face();
    
    ESP_LOGI(TAG, "Tire pressure gauge initialized");
}

void tire_pressure_set_night_mode(bool is_night_mode)
{
    if (night_mode == is_night_mode) {
        return; // No change needed
    }
    
    night_mode = is_night_mode;
    ESP_LOGI(TAG, "Setting %s mode", night_mode ? "night" : "day");
    
    // Redraw gauge face to update shadow colors
    draw_gauge_face();
}

void tire_pressure_set_visible(bool visible)
{
    if (gauge_container) {
        if (visible) {
            lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void tire_pressure_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up tire pressure gauge");
    
    if (gauge_container) {
        lv_obj_del(gauge_container);
        gauge_container = NULL;
        roof_image = NULL;
        for (int i = 0; i < 4; i++) {
            pressure_labels[i] = NULL;
        }
        units_label = NULL;
    }
    
    ESP_LOGI(TAG, "Tire pressure gauge cleanup complete");
}

void tire_pressure_set_value(int wheel, float pressure_psi)
{
    if (wheel < 0 || wheel > 3) return;
    
    pressure_values[wheel] = pressure_psi;
    update_pressure_label(wheel);
}

void tire_pressure_set_all_values(float fl, float fr, float rl, float rr)
{
    pressure_values[0] = fl;
    pressure_values[1] = fr;
    pressure_values[2] = rl;
    pressure_values[3] = rr;
    update_all_pressure_labels();
}

void tire_pressure_toggle_units(void)
{
    use_bar_units = !use_bar_units;
    update_units_label();
    update_all_pressure_labels();
    ESP_LOGI(TAG, "Units toggled to %s", use_bar_units ? "bar" : "psi");
}

void tire_pressure_set_units_bar(bool use_bar)
{
    if (use_bar_units == use_bar) return;
    
    use_bar_units = use_bar;
    update_units_label();
    update_all_pressure_labels();
    ESP_LOGI(TAG, "Units set to %s", use_bar_units ? "bar" : "psi");
}
