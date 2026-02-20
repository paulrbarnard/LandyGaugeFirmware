/**
 * @file artificial_horizon.c
 * @brief Artificial horizon (attitude indicator) gauge implementation
 */

#include "artificial_horizon.h"
#include "lvgl.h"
#include "esp_log.h"
#include "LVGL_Driver/style.h"
#include "WarningBeep/warning_beep.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "ARTIFICIAL_HORIZON";

// Display parameters (DISP_W and DISP_H from style.h)
#define HORIZON_SIZE DISP_W  // Main instrument size
#define HORIZON_CENTER_X (DISP_W / 2)
#define HORIZON_CENTER_Y (DISP_H / 2)

// Colors
static lv_color_t sky_color;
static lv_color_t ground_color;
static lv_color_t pitch_line_color;  // Color for pitch lines (warning based on pitch)
static lv_color_t roll_line_color;   // Color for roll scale/pointer (warning based on roll)
static bool night_mode = true;

// Warning thresholds
#define PITCH_YELLOW_THRESHOLD 35.0f
#define PITCH_RED_THRESHOLD 45.0f
#define ROLL_YELLOW_THRESHOLD 30.0f
#define ROLL_RED_THRESHOLD 35.0f  

// LVGL objects
static lv_obj_t *horizon_container = NULL;
static lv_obj_t *sky_obj = NULL;
static lv_obj_t *pitch_lines_obj = NULL;
static lv_obj_t *roll_scale_obj = NULL;    // Static roll scale (tick marks + arc)
static lv_obj_t *roll_pointer_obj = NULL;  // Moving roll pointer
static lv_obj_t *center_marker_obj = NULL;

// Pixels per degree for pitch movement
#define PIXELS_PER_DEGREE 3.0f

// Smoothing filter coefficient (0.0 = no smoothing, 1.0 = instant response)
// Lower values = more inertia/smoothing, higher values = faster response
#define SMOOTHING_FACTOR 0.15f

// Current attitude (smoothed values)
static float current_pitch = 0.0f;
static float current_roll = 0.0f;

// Raw target values from sensor
static float target_pitch = 0.0f;
static float target_roll = 0.0f;

// Warning state tracking
typedef enum {
    WARNING_NONE,
    WARNING_YELLOW,
    WARNING_RED
} warning_level_t;
static warning_level_t pitch_warning_level = WARNING_NONE;
static warning_level_t roll_warning_level = WARNING_NONE;
static warning_level_t current_roll_audio_level = WARNING_NONE;   // Track active roll audio level
static warning_level_t current_pitch_audio_level = WARNING_NONE;  // Track active pitch audio level

/**
 * @brief Get warning color for pitch based on current pitch angle
 */
static lv_color_t get_pitch_warning_color(void)
{
    float abs_pitch = fabsf(current_pitch);
    
    if (abs_pitch >= PITCH_RED_THRESHOLD) {
        return lv_color_make(255, 0, 0);  // Red
    }
    else if (abs_pitch >= PITCH_YELLOW_THRESHOLD) {
        return lv_color_make(255, 255, 0);  // Yellow
    }
    else {
        return get_accent_color(night_mode);
    }
}

/**
 * @brief Get warning color for roll based on current roll angle
 */
static lv_color_t get_roll_warning_color(void)
{
    float abs_roll = fabsf(current_roll);
    
    if (abs_roll >= ROLL_RED_THRESHOLD) {
        return lv_color_make(255, 0, 0);  // Red
    }
    else if (abs_roll >= ROLL_YELLOW_THRESHOLD) {
        return lv_color_make(255, 255, 0);  // Yellow
    }
    else {
        return get_accent_color(night_mode);
    }
}

/**
 * @brief Draw callback for the sky/ground split
 * Note: Container movement handles pitch offset, so we draw at center
 */
static void draw_sky_ground_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    int32_t radius = HORIZON_SIZE / 2;
    
    // Draw sky (upper half) - horizon is at center, container moves for pitch
    lv_draw_rect_dsc_t sky_dsc;
    lv_draw_rect_dsc_init(&sky_dsc);
    sky_dsc.bg_color = sky_color;
    sky_dsc.bg_opa = LV_OPA_COVER;
    sky_dsc.radius = radius;
    
    lv_area_t sky_area;
    sky_area.x1 = center_x - radius;
    sky_area.x2 = center_x + radius;
    sky_area.y1 = center_y - radius;
    sky_area.y2 = center_y;  // Fixed at center
    
    lv_draw_rect(draw_ctx, &sky_dsc, &sky_area);
    
    // Draw ground (lower half)
    lv_draw_rect_dsc_t ground_dsc;
    lv_draw_rect_dsc_init(&ground_dsc);
    ground_dsc.bg_color = ground_color;
    ground_dsc.bg_opa = LV_OPA_COVER;
    ground_dsc.radius = radius;
    
    lv_area_t ground_area;
    ground_area.x1 = center_x - radius;
    ground_area.x2 = center_x + radius;
    ground_area.y1 = center_y;  // Fixed at center
    ground_area.y2 = center_y + radius;
    
    lv_draw_rect(draw_ctx, &ground_dsc, &ground_area);
    
    // Draw horizon line at center
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = pitch_line_color;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    
    lv_point_t line_points[2];
    line_points[0].x = center_x - radius;
    line_points[0].y = center_y;  // Fixed at center
    line_points[1].x = center_x + radius;
    line_points[1].y = center_y;  // Fixed at center
    
    lv_draw_line(draw_ctx, &line_dsc, &line_points[0], &line_points[1]);
}

/**
 * @brief Draw callback for pitch ladder lines
 * Note: Lines are drawn at fixed positions, container movement handles pitch offset
 */
static void draw_pitch_lines_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = pitch_line_color;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = pitch_line_color;
    label_dsc.opa = LV_OPA_COVER;
    
    // Draw pitch lines every 10 degrees at fixed positions
    // Container moves to show correct pitch, lines are static relative to container
    for (int pitch_angle = -90; pitch_angle <= 90; pitch_angle += 10) {
        if (pitch_angle == 0) continue;  // Skip zero (main horizon line)
        
        // Fixed Y position based on pitch angle (negative pitch = line below center)
        int32_t y_pos = center_y - (int32_t)(pitch_angle * PIXELS_PER_DEGREE);
        
        int32_t line_width = (pitch_angle % 90 == 0) ? 80 : 60;
        
        lv_point_t line_points[2];
        line_points[0].x = center_x - line_width;
        line_points[0].y = y_pos;
        line_points[1].x = center_x + line_width;
        line_points[1].y = y_pos;
        
        lv_draw_line(draw_ctx, &line_dsc, &line_points[0], &line_points[1]);
        
        // Add numeric labels for multiples of 10
        if (pitch_angle % 10 == 0 && pitch_angle != 0) {
            char label_text[8];
            snprintf(label_text, sizeof(label_text), "%d", pitch_angle > 0 ? pitch_angle : -pitch_angle);
            
            lv_area_t label_area;
            label_area.x1 = center_x + line_width + 5;
            label_area.y1 = y_pos - 8;
            label_area.x2 = label_area.x1 + 20;
            label_area.y2 = label_area.y1 + 16;
            
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
            
            // Mirror on left side
            label_area.x1 = center_x - line_width - 25;
            label_area.x2 = label_area.x1 + 20;
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
        }
    }
}

/**
 * @brief Draw callback for static roll scale (curved arc on right side)
 * Range: ±45 degrees, arc on right side of gauge
 */
static void draw_roll_scale_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    int32_t radius = HORIZON_SIZE / 2 - 15;  // Inset from edge
    
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = roll_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    
    // Draw roll scale arc on right side (from -45° to +45° displayed vertically)
    // LVGL angles: 0° at 3 o'clock, increases clockwise
    // We want: +45° roll at top (315° LVGL), 0° roll at right (0° LVGL), -45° roll at bottom (45° LVGL)
    lv_point_t center_point;
    center_point.x = center_x;
    center_point.y = center_y;
    
    // Arc from 315° to 45° (90° span on right side)
    lv_draw_arc(draw_ctx, &arc_dsc, &center_point, radius, 315, 405);  // 405 = 45 + 360 to cross 0°
    
    // Draw tick marks
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = roll_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa = LV_OPA_COVER;
    
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = roll_line_color;
    label_dsc.opa = LV_OPA_COVER;
    
    // Tick marks: 0, ±15, ±30, ±45
    int tick_angles[] = {-45, -30, -15, 0, 15, 30, 45};
    for (int i = 0; i < 7; i++) {
        int roll_angle = tick_angles[i];
        // Convert roll angle to LVGL drawing angle
        // +roll = up = smaller LVGL angle (toward 315°)
        // -roll = down = larger LVGL angle (toward 45°)
        float lvgl_angle = -roll_angle;  // Roll angle in degrees from horizontal
        float angle_rad = lvgl_angle * M_PI / 180.0f;
        
        int tick_len = (roll_angle == 0) ? 15 : ((roll_angle % 30 == 0) ? 12 : 8);
        
        int32_t x1 = center_x + (int32_t)(radius * cosf(angle_rad));
        int32_t y1 = center_y + (int32_t)(radius * sinf(angle_rad));
        int32_t x2 = center_x + (int32_t)((radius - tick_len) * cosf(angle_rad));
        int32_t y2 = center_y + (int32_t)((radius - tick_len) * sinf(angle_rad));
        
        lv_point_t tick_points[2];
        tick_points[0].x = x1;
        tick_points[0].y = y1;
        tick_points[1].x = x2;
        tick_points[1].y = y2;
        
        lv_draw_line(draw_ctx, &tick_dsc, &tick_points[0], &tick_points[1]);
        
        // Add numeric labels for major ticks (skip 0)
        if (roll_angle != 0 && roll_angle % 15 == 0) {
            char label_text[12];
            snprintf(label_text, sizeof(label_text), "%d", roll_angle > 0 ? roll_angle : -roll_angle);
            
            lv_area_t label_area;
            label_area.x1 = center_x + (int32_t)((radius - 28) * cosf(angle_rad)) - 10;
            label_area.y1 = center_y + (int32_t)((radius - 28) * sinf(angle_rad)) - 8;
            label_area.x2 = label_area.x1 + 20;
            label_area.y2 = label_area.y1 + 16;
            
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
        }
    }
}

/**
 * @brief Draw callback for roll pointer (triangle that moves along curved arc)
 */
static void draw_roll_pointer_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    int32_t radius = HORIZON_SIZE / 2 - 15;  // Same as scale
    
    // Clamp roll to ±45 degrees for display
    float clamped_roll = current_roll;
    if (clamped_roll > 45.0f) clamped_roll = 45.0f;
    if (clamped_roll < -45.0f) clamped_roll = -45.0f;
    
    // Convert roll to angle on arc
    // +roll = up on arc = negative LVGL angle
    float angle_rad = clamped_roll * M_PI / 180.0f;
    
    // Draw pointer triangle pointing inward (toward center)
    lv_draw_rect_dsc_t tri_dsc;
    lv_draw_rect_dsc_init(&tri_dsc);
    tri_dsc.bg_color = roll_line_color;
    tri_dsc.bg_opa = LV_OPA_COVER;
    
    // Tip of triangle at the arc
    int32_t tip_x = center_x + (int32_t)((radius + 5) * cosf(angle_rad));
    int32_t tip_y = center_y + (int32_t)((radius + 5) * sinf(angle_rad));
    
    // Base of triangle (further out, perpendicular to radius)
    int base_offset = 8;
    float perp_angle = angle_rad + M_PI / 2.0f;
    int32_t base_x = center_x + (int32_t)((radius + 18) * cosf(angle_rad));
    int32_t base_y = center_y + (int32_t)((radius + 18) * sinf(angle_rad));
    
    lv_point_t tri_points[3];
    tri_points[0].x = tip_x;
    tri_points[0].y = tip_y;
    tri_points[1].x = base_x + (int32_t)(base_offset * cosf(perp_angle));
    tri_points[1].y = base_y + (int32_t)(base_offset * sinf(perp_angle));
    tri_points[2].x = base_x - (int32_t)(base_offset * cosf(perp_angle));
    tri_points[2].y = base_y - (int32_t)(base_offset * sinf(perp_angle));
    
    lv_draw_triangle(draw_ctx, &tri_dsc, tri_points);
}

/**
 * @brief Draw callback for center aircraft marker
 */
static void draw_center_marker_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_make(255, 200, 0);  // Yellow/orange
    line_dsc.width = 4;
    line_dsc.opa = LV_OPA_COVER;
    
    // Draw center dot
    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.bg_color = lv_color_make(255, 200, 0);
    dot_dsc.bg_opa = LV_OPA_COVER;
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    
    lv_area_t dot_area;
    dot_area.x1 = center_x - 5;
    dot_area.x2 = center_x + 5;
    dot_area.y1 = center_y - 5;
    dot_area.y2 = center_y + 5;
    
    lv_draw_rect(draw_ctx, &dot_dsc, &dot_area);
    
    // Draw left wing
    lv_point_t left_wing[2];
    left_wing[0].x = center_x - 10;
    left_wing[0].y = center_y;
    left_wing[1].x = center_x - 80;
    left_wing[1].y = center_y;
    lv_draw_line(draw_ctx, &line_dsc, &left_wing[0], &left_wing[1]);
    
    // Draw right wing
    lv_point_t right_wing[2];
    right_wing[0].x = center_x + 10;
    right_wing[0].y = center_y;
    right_wing[1].x = center_x + 80;
    right_wing[1].y = center_y;
    lv_draw_line(draw_ctx, &line_dsc, &right_wing[0], &right_wing[1]);
}

void artificial_horizon_init(void)
{
    ESP_LOGD(TAG, "Initializing artificial horizon");
    
    // Initialize warning beep system
    warning_beep_init();
    current_roll_audio_level = WARNING_NONE;
    current_pitch_audio_level = WARNING_NONE;
    
    // Set initial colors (night mode will be set by default)
    sky_color = lv_color_make(0, 50, 100);      // Dark blue for night sky
    ground_color = lv_color_make(60, 40, 20);   // Dark brown for night ground
    pitch_line_color = get_accent_color(night_mode);
    roll_line_color = get_accent_color(night_mode);
    
    // Create main container with dark grey background (full screen fill)
    horizon_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(horizon_container, DISP_W, DISP_H);
    lv_obj_center(horizon_container);
    lv_obj_set_style_bg_color(horizon_container, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_bg_opa(horizon_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(horizon_container, 0, 0);
    lv_obj_set_style_pad_all(horizon_container, 0, 0);
    lv_obj_clear_flag(horizon_container, LV_OBJ_FLAG_SCROLLABLE);  // Prevent scrolling
    lv_obj_clear_flag(horizon_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // Clip children to container
    
    // Create sky/ground object
    sky_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(sky_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(sky_obj);
    lv_obj_set_style_bg_opa(sky_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sky_obj, 0, 0);
    lv_obj_add_event_cb(sky_obj, draw_sky_ground_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create pitch lines object
    pitch_lines_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(pitch_lines_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(pitch_lines_obj);
    lv_obj_set_style_bg_opa(pitch_lines_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pitch_lines_obj, 0, 0);
    lv_obj_add_event_cb(pitch_lines_obj, draw_pitch_lines_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create roll scale object (static arc and tick marks)
    roll_scale_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(roll_scale_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(roll_scale_obj);
    lv_obj_set_style_bg_opa(roll_scale_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(roll_scale_obj, 0, 0);
    lv_obj_add_event_cb(roll_scale_obj, draw_roll_scale_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create roll pointer object (moving triangle)
    roll_pointer_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(roll_pointer_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(roll_pointer_obj);
    lv_obj_set_style_bg_opa(roll_pointer_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(roll_pointer_obj, 0, 0);
    lv_obj_add_event_cb(roll_pointer_obj, draw_roll_pointer_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create center marker object (aircraft symbol)
    center_marker_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(center_marker_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(center_marker_obj);
    lv_obj_set_style_bg_opa(center_marker_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_marker_obj, 0, 0);
    lv_obj_add_event_cb(center_marker_obj, draw_center_marker_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create shadow effects on top (light leak effect from side-lit gauge)
    //create_gauge_shadows(horizon_container, night_mode);
    
    ESP_LOGD(TAG, "Artificial horizon initialized");
}

void artificial_horizon_update(float pitch, float roll)
{
    // Clamp values to valid ranges
    if (pitch > 90.0f) pitch = 90.0f;
    if (pitch < -90.0f) pitch = -90.0f;
    if (roll > 180.0f) roll = 180.0f;
    if (roll < -180.0f) roll = -180.0f;
    
    // Store target values
    target_pitch = pitch;
    target_roll = roll;
    
    // Apply exponential moving average (EMA) smoothing for inertia effect
    // new_value = old_value + factor * (target - old_value)
    float prev_pitch = current_pitch;
    float prev_roll = current_roll;
    
    current_pitch = current_pitch + SMOOTHING_FACTOR * (target_pitch - current_pitch);
    current_roll = current_roll + SMOOTHING_FACTOR * (target_roll - current_roll);
    
    // Check if smoothed values changed significantly (threshold for redraw)
    bool pitch_changed = (fabsf(current_pitch - prev_pitch) > 0.1f);
    bool roll_changed = (fabsf(current_roll - prev_roll) > 0.1f);
    
    // Update line colors based on individual attitude warnings
    lv_color_t new_pitch_color = get_pitch_warning_color();
    lv_color_t new_roll_color = get_roll_warning_color();
    
    // Check if pitch color changed (need to redraw pitch elements)
    bool pitch_color_changed = (pitch_line_color.full != new_pitch_color.full);
    bool roll_color_changed = (roll_line_color.full != new_roll_color.full);
    
    pitch_line_color = new_pitch_color;
    roll_line_color = new_roll_color;
    
    // Determine individual warning levels for logging
    float abs_pitch = fabsf(current_pitch);
    float abs_roll = fabsf(current_roll);
    
    warning_level_t new_pitch_warning;
    if (abs_pitch >= PITCH_RED_THRESHOLD) {
        new_pitch_warning = WARNING_RED;
    } else if (abs_pitch >= PITCH_YELLOW_THRESHOLD) {
        new_pitch_warning = WARNING_YELLOW;
    } else {
        new_pitch_warning = WARNING_NONE;
    }
    
    warning_level_t new_roll_warning;
    if (abs_roll >= ROLL_RED_THRESHOLD) {
        new_roll_warning = WARNING_RED;
    } else if (abs_roll >= ROLL_YELLOW_THRESHOLD) {
        new_roll_warning = WARNING_YELLOW;
    } else {
        new_roll_warning = WARNING_NONE;
    }
    
    // Log pitch warning changes
    if (new_pitch_warning != pitch_warning_level) {
        pitch_warning_level = new_pitch_warning;
        if (pitch_warning_level == WARNING_RED) {
            ESP_LOGW(TAG, "PITCH RED WARNING: pitch=%.1f", abs_pitch);
        } else if (pitch_warning_level == WARNING_YELLOW) {
            ESP_LOGW(TAG, "PITCH YELLOW WARNING: pitch=%.1f", abs_pitch);
        }
    }
    
    // Log roll warning changes
    if (new_roll_warning != roll_warning_level) {
        roll_warning_level = new_roll_warning;
        if (roll_warning_level == WARNING_RED) {
            ESP_LOGW(TAG, "ROLL RED WARNING: roll=%.1f", abs_roll);
        } else if (roll_warning_level == WARNING_YELLOW) {
            ESP_LOGW(TAG, "ROLL YELLOW WARNING: roll=%.1f", abs_roll);
        }
    }
    
    // Update ROLL warning audio (beep + MP3)
    if (new_roll_warning != current_roll_audio_level) {
        current_roll_audio_level = new_roll_warning;
        
        if (current_roll_audio_level == WARNING_RED) {
            warning_beep_start(WARNING_LEVEL_RED);
            ESP_LOGI(TAG, "Roll DANGER warning started (DangerRoll.mp3)");
        } else if (current_roll_audio_level == WARNING_YELLOW) {
            warning_beep_start(WARNING_LEVEL_YELLOW);
            ESP_LOGI(TAG, "Roll WARNING started (WarningRoll.mp3)");
        } else {
            warning_beep_start(WARNING_LEVEL_NONE);  // Stop roll audio
            ESP_LOGI(TAG, "Roll warning audio stopped");
        }
    }
    
    // Update PITCH warning audio (beeps only)
    if (new_pitch_warning != current_pitch_audio_level) {
        current_pitch_audio_level = new_pitch_warning;
        
        if (current_pitch_audio_level == WARNING_RED) {
            warning_pitch_start(WARNING_LEVEL_RED);
            ESP_LOGI(TAG, "Pitch RED beeps started (every 500ms)");
        } else if (current_pitch_audio_level == WARNING_YELLOW) {
            warning_pitch_start(WARNING_LEVEL_YELLOW);
            ESP_LOGI(TAG, "Pitch YELLOW beeps started (every 3s)");
        } else {
            warning_pitch_start(WARNING_LEVEL_NONE);  // Stop pitch beeps
            ESP_LOGI(TAG, "Pitch warning beeps stopped");
        }
    }
    
    // Move containers for pitch changes (much faster than redrawing)
    if (pitch_changed) {
        // Positive pitch (nose up) = horizon moves DOWN = positive y offset
        int32_t pitch_offset = (int32_t)(-current_pitch * PIXELS_PER_DEGREE);
        
        // Move sky/ground and pitch lines containers
        if (sky_obj) {
            lv_obj_set_y(sky_obj, pitch_offset);
        }
        if (pitch_lines_obj) {
            lv_obj_set_y(pitch_lines_obj, pitch_offset);
        }
    }
    
    // Invalidate pitch elements if color changed
    if (pitch_color_changed) {
        if (sky_obj) lv_obj_invalidate(sky_obj);
        if (pitch_lines_obj) lv_obj_invalidate(pitch_lines_obj);
    }
    
    // Invalidate roll elements if roll changed or color changed
    if ((roll_changed || roll_color_changed) && roll_pointer_obj) {
        lv_obj_invalidate(roll_pointer_obj);
    }
    if (roll_color_changed && roll_scale_obj) {
        lv_obj_invalidate(roll_scale_obj);
    }
}

void artificial_horizon_set_night_mode(bool night)
{
    night_mode = night;
    pitch_line_color = get_pitch_warning_color();
    roll_line_color = get_roll_warning_color();
    
    // Update sky and ground colors for night mode
    if (night_mode) {
        sky_color = lv_color_make(0, 50, 100);      // Dark blue for night sky
        ground_color = lv_color_make(60, 40, 20);   // Dark brown for night ground
    } else {
        sky_color = lv_color_make(0, 150, 255);     // Bright blue for day sky
        ground_color = lv_color_make(139, 90, 43);  // Brown earth for day
    }
    
    // Redraw all objects with new colors
    if (horizon_container) {
        lv_obj_invalidate(horizon_container);
    }
}

void artificial_horizon_set_visible(bool visible)
{
    if (horizon_container) {
        if (visible) {
            lv_obj_clear_flag(horizon_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(horizon_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void artificial_horizon_cleanup(void)
{
    ESP_LOGD(TAG, "Cleaning up artificial horizon");
    
    // Stop warning beeps
    warning_beep_stop();
    current_roll_audio_level = WARNING_NONE;
    current_pitch_audio_level = WARNING_NONE;
    
    // Delete the main container (this also deletes all children)
    if (horizon_container) {
        lv_obj_del(horizon_container);
        horizon_container = NULL;
    }
    
    // Reset all object pointers
    sky_obj = NULL;
    pitch_lines_obj = NULL;
    roll_scale_obj = NULL;
    roll_pointer_obj = NULL;
    center_marker_obj = NULL;
    
    // Reset state
    current_pitch = 0.0f;
    current_roll = 0.0f;
    pitch_warning_level = WARNING_NONE;
    roll_warning_level = WARNING_NONE;
    
    // ESP_LOGD(TAG, "Artificial horizon cleaned up");
}
