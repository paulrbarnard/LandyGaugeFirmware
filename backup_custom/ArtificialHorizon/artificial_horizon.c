/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file artificial_horizon.c
 * @brief Artificial horizon (attitude indicator) gauge implementation
 */

#include "artificial_horizon.h"
#include "lvgl.h"
#include "esp_log.h"
#include "warning_beep.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "ARTIFICIAL_HORIZON";

// Display parameters
#define SCREEN_WIDTH 360
#define SCREEN_HEIGHT 360
#define HORIZON_SIZE 330  // Main instrument size
#define HORIZON_CENTER_X (SCREEN_WIDTH / 2)
#define HORIZON_CENTER_Y (SCREEN_HEIGHT / 2)

// Colors
static lv_color_t sky_color;
static lv_color_t ground_color;
static lv_color_t line_color;
static bool night_mode = false;

// Warning thresholds
#define PITCH_YELLOW_THRESHOLD 35.0f
#define PITCH_RED_THRESHOLD 45.0f
#define ROLL_YELLOW_THRESHOLD 30.0f
#define ROLL_RED_THRESHOLD 35.0f

// LVGL objects
static lv_obj_t *horizon_container = NULL;
static lv_obj_t *shadow_light = NULL;  // Outer ring highlight
static lv_obj_t *sky_obj = NULL;
static lv_obj_t *pitch_lines_obj = NULL;
static lv_obj_t *roll_indicator_obj = NULL;
static lv_obj_t *center_marker_obj = NULL;

// Current attitude
static float current_pitch = 0.0f;
static float current_roll = 0.0f;

// Warning state tracking
typedef enum {
    WARNING_NONE,
    WARNING_YELLOW,
    WARNING_RED
} warning_level_t;
static warning_level_t current_warning_level = WARNING_NONE;

/**
 * @brief Get primary color based on day/night mode
 */
static lv_color_t get_primary_color(void)
{
    if (night_mode) {
        return lv_color_make(0, 255, 0);  // Green for night mode
    } else {
        return lv_color_white();  // White for day mode
    }
}

/**
 * @brief Get warning color based on current attitude
 */
static lv_color_t get_warning_color(void)
{
    float abs_pitch = fabsf(current_pitch);
    float abs_roll = fabsf(current_roll);
    
    // Red if either exceeds red threshold
    if (abs_pitch >= PITCH_RED_THRESHOLD || abs_roll >= ROLL_RED_THRESHOLD) {
        return lv_color_make(255, 0, 0);  // Red
    }
    // Yellow if either exceeds yellow threshold
    else if (abs_pitch >= PITCH_YELLOW_THRESHOLD || abs_roll >= ROLL_YELLOW_THRESHOLD) {
        return lv_color_make(255, 255, 0);  // Yellow
    }
    // Normal white/green
    else {
        return get_primary_color();
    }
}

/**
 * @brief Draw callback for the sky/ground split
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
    
    // Calculate horizon line position based on pitch and roll
    // Pitch: pixels per degree (adjust sensitivity)
    float pixels_per_degree = 3.0f;
    int32_t pitch_offset = (int32_t)(current_pitch * pixels_per_degree);
    
    // Draw sky (upper half)
    lv_draw_rect_dsc_t sky_dsc;
    lv_draw_rect_dsc_init(&sky_dsc);
    sky_dsc.bg_color = sky_color;
    sky_dsc.bg_opa = LV_OPA_COVER;
    sky_dsc.radius = radius;
    
    lv_area_t sky_area;
    sky_area.x1 = center_x - radius;
    sky_area.x2 = center_x + radius;
    sky_area.y1 = center_y - radius;
    sky_area.y2 = center_y - pitch_offset;
    
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
    ground_area.y1 = center_y - pitch_offset;
    ground_area.y2 = center_y + radius;
    
    lv_draw_rect(draw_ctx, &ground_dsc, &ground_area);
    
    // Draw horizon line
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = line_color;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    
    lv_point_t line_points[2];
    line_points[0].x = center_x - radius;
    line_points[0].y = center_y - pitch_offset;
    line_points[1].x = center_x + radius;
    line_points[1].y = center_y - pitch_offset;
    
    lv_draw_line(draw_ctx, &line_dsc, &line_points[0], &line_points[1]);
}

/**
 * @brief Draw callback for pitch ladder lines
 */
static void draw_pitch_lines_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    
    float pixels_per_degree = 3.0f;
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = line_color;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = line_color;
    label_dsc.opa = LV_OPA_COVER;
    
    // Draw pitch lines every 10 degrees
    for (int pitch_angle = -90; pitch_angle <= 90; pitch_angle += 10) {
        if (pitch_angle == 0) continue;  // Skip zero (main horizon line)
        
        int32_t y_offset = (int32_t)((current_pitch - pitch_angle) * pixels_per_degree);
        int32_t y_pos = center_y - y_offset;
        
        // Only draw if visible and below roll scale (avoid top 60 pixels and bottom 60 pixels)
        int32_t top_limit = obj_coords.y1 + 60;
        int32_t bottom_limit = obj_coords.y2 - 60;
        if (y_pos < top_limit || y_pos > bottom_limit) continue;
        
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
 * @brief Draw callback for roll indicator
 */
static void draw_roll_indicator_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    int32_t radius = HORIZON_SIZE / 2;
    
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = line_color;
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    
    // Draw roll scale arc (top of instrument)
    lv_point_t center_point;
    center_point.x = center_x;
    center_point.y = center_y;
    
    lv_draw_arc(draw_ctx, &arc_dsc, &center_point, radius - 10, 30, 150);
    
    // Draw roll tick marks
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = line_color;
    tick_dsc.width = 2;
    tick_dsc.opa = LV_OPA_COVER;
    
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = line_color;
    label_dsc.opa = LV_OPA_COVER;
    
    int tick_angles[] = {0, 10, 20, 30, 45, 60};
    for (int i = 0; i < 6; i++) {
        for (int side = -1; side <= 1; side += 2) {  // Both sides
            int angle = 90 - (tick_angles[i] * side);  // Convert to LVGL angle
            float angle_rad = angle * M_PI / 180.0f;
            
            int tick_len = (tick_angles[i] % 30 == 0) ? 15 : 10;
            
            int32_t x1 = center_x + (int32_t)((radius - 10) * cosf(angle_rad));
            int32_t y1 = center_y - (int32_t)((radius - 10) * sinf(angle_rad));
            int32_t x2 = center_x + (int32_t)((radius - 10 - tick_len) * cosf(angle_rad));
            int32_t y2 = center_y - (int32_t)((radius - 10 - tick_len) * sinf(angle_rad));
            
            lv_point_t tick_points[2];
            tick_points[0].x = x1;
            tick_points[0].y = y1;
            tick_points[1].x = x2;
            tick_points[1].y = y2;
            
            lv_draw_line(draw_ctx, &tick_dsc, &tick_points[0], &tick_points[1]);
            
            // Add numeric labels for multiples of 10 and 45 (skip 0)
            if (((tick_angles[i] % 10 == 0) || tick_angles[i] == 45) && tick_angles[i] != 0) {
                char label_text[8];
                snprintf(label_text, sizeof(label_text), "%d", tick_angles[i]);
                
                lv_area_t label_area;
                label_area.x1 = center_x + (int32_t)((radius - 30) * cosf(angle_rad)) - 8;
                label_area.y1 = center_y - (int32_t)((radius - 30) * sinf(angle_rad)) - 8;
                label_area.x2 = label_area.x1 + 20;
                label_area.y2 = label_area.y1 + 16;
                
                lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
            }
        }
    }
    
    // Draw roll pointer (triangle at current roll angle)
    float roll_rad = (90.0f - current_roll) * M_PI / 180.0f;
    
    lv_draw_rect_dsc_t tri_dsc;
    lv_draw_rect_dsc_init(&tri_dsc);
    tri_dsc.bg_color = line_color;
    tri_dsc.bg_opa = LV_OPA_COVER;
    
    lv_point_t tri_points[3];
    // Point of triangle at roll angle
    int32_t pointer_x = center_x + (int32_t)((radius - 5) * cosf(roll_rad));
    int32_t pointer_y = center_y - (int32_t)((radius - 5) * sinf(roll_rad));
    tri_points[0].x = pointer_x;
    tri_points[0].y = pointer_y;
    // Base points
    int base_offset = 8;
    float perp_angle = roll_rad + M_PI / 2.0f;
    tri_points[1].x = center_x + (int32_t)((radius - 20) * cosf(roll_rad)) + (int32_t)(base_offset * cosf(perp_angle));
    tri_points[1].y = center_y - (int32_t)((radius - 20) * sinf(roll_rad)) - (int32_t)(base_offset * sinf(perp_angle));
    tri_points[2].x = center_x + (int32_t)((radius - 20) * cosf(roll_rad)) - (int32_t)(base_offset * cosf(perp_angle));
    tri_points[2].y = center_y - (int32_t)((radius - 20) * sinf(roll_rad)) + (int32_t)(base_offset * sinf(perp_angle));
    
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
    ESP_LOGI(TAG, "Initializing artificial horizon");
    
    // Set initial colors (night mode will be set by default)
    sky_color = lv_color_make(0, 50, 100);      // Dark blue for night sky
    ground_color = lv_color_make(60, 40, 20);   // Dark brown for night ground
    line_color = get_primary_color();
    
    // Create main container with dark grey background (full screen fill)
    horizon_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(horizon_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(horizon_container, 0, 0);
    lv_obj_set_style_bg_color(horizon_container, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_bg_opa(horizon_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(horizon_container, 0, 0);
    lv_obj_set_style_pad_all(horizon_container, 0, 0);
    
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
    
    // Create roll indicator object
    roll_indicator_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(roll_indicator_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(roll_indicator_obj);
    lv_obj_set_style_bg_opa(roll_indicator_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(roll_indicator_obj, 0, 0);
    lv_obj_add_event_cb(roll_indicator_obj, draw_roll_indicator_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create center marker object (aircraft symbol)
    center_marker_obj = lv_obj_create(horizon_container);
    lv_obj_set_size(center_marker_obj, HORIZON_SIZE, HORIZON_SIZE);
    lv_obj_center(center_marker_obj);
    lv_obj_set_style_bg_opa(center_marker_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_marker_obj, 0, 0);
    lv_obj_add_event_cb(center_marker_obj, draw_center_marker_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create masking ring (dark grey, 20px wide border ring)
    lv_obj_t *mask_ring = lv_obj_create(horizon_container);
    lv_obj_set_size(mask_ring, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(mask_ring);
    lv_obj_set_style_radius(mask_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mask_ring, LV_OPA_TRANSP, 0);  // Transparent center
    lv_obj_set_style_border_width(mask_ring, 15, 0);  // 20px border ring
    lv_obj_set_style_border_color(mask_ring, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_border_opa(mask_ring, LV_OPA_COVER, 0);
    
    // Create black shadow effect (top-left)
    lv_obj_t *shadow_dark = lv_obj_create(horizon_container);
    lv_obj_set_size(shadow_dark, HORIZON_SIZE - 8, HORIZON_SIZE - 10);
    lv_obj_center(shadow_dark);
    lv_obj_set_style_radius(shadow_dark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(shadow_dark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shadow_dark, 0, 0);
    lv_obj_set_style_shadow_width(shadow_dark, 15, 0);
    lv_obj_set_style_shadow_opa(shadow_dark, LV_OPA_70, 0);
    lv_obj_set_style_shadow_color(shadow_dark, lv_color_black(), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_dark, -4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_dark, -4, 0);
    
    // Create light shadow effect (bottom-right)
    shadow_light = lv_obj_create(horizon_container);
    lv_obj_set_size(shadow_light, HORIZON_SIZE - 8, HORIZON_SIZE - 10);
    lv_obj_center(shadow_light);
    lv_obj_set_style_radius(shadow_light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(shadow_light, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shadow_light, 0, 0);
    lv_obj_set_style_shadow_width(shadow_light, 15, 0);
    lv_obj_set_style_shadow_opa(shadow_light, LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(shadow_light, get_primary_color(), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_light, 4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_light, 4, 0);
    
    ESP_LOGI(TAG, "Artificial horizon initialized");
}

void artificial_horizon_update(float pitch, float roll)
{
    // Clamp values to valid ranges
    if (pitch > 90.0f) pitch = 90.0f;
    if (pitch < -90.0f) pitch = -90.0f;
    if (roll > 180.0f) roll = 180.0f;
    if (roll < -180.0f) roll = -180.0f;
    
    current_pitch = pitch;
    current_roll = roll;
    
    // Update line color based on attitude warnings
    line_color = get_warning_color();
    
    // Determine warning level
    float abs_pitch = fabsf(current_pitch);
    float abs_roll = fabsf(current_roll);
    warning_level_t new_warning_level;
    
    if (abs_pitch >= PITCH_RED_THRESHOLD || abs_roll >= ROLL_RED_THRESHOLD) {
        new_warning_level = WARNING_RED;
    } else if (abs_pitch >= PITCH_YELLOW_THRESHOLD || abs_roll >= ROLL_YELLOW_THRESHOLD) {
        new_warning_level = WARNING_YELLOW;
    } else {
        new_warning_level = WARNING_NONE;
    }
    
    // Update beep pattern if warning level changed
    if (new_warning_level != current_warning_level) {
        current_warning_level = new_warning_level;
        
        switch (current_warning_level) {
            case WARNING_RED:
                // Short beep every 0.5 seconds
                warning_beep_repeat(BEEP_SHORT, 500);
                ESP_LOGW(TAG, "RED WARNING: pitch=%.1f, roll=%.1f", abs_pitch, abs_roll);
                break;
                
            case WARNING_YELLOW:
                // Short beep every 2 seconds
                warning_beep_repeat(BEEP_SHORT, 2000);
                ESP_LOGW(TAG, "YELLOW WARNING: pitch=%.1f, roll=%.1f", abs_pitch, abs_roll);
                break;
                
            case WARNING_NONE:
                // Stop beeping
                warning_beep_stop();
                break;
        }
    }
    
    // Invalidate objects to trigger redraw
    if (sky_obj) lv_obj_invalidate(sky_obj);
    if (pitch_lines_obj) lv_obj_invalidate(pitch_lines_obj);
    if (roll_indicator_obj) lv_obj_invalidate(roll_indicator_obj);
}

void artificial_horizon_set_night_mode(bool night)
{
    night_mode = night;
    line_color = get_primary_color();
    
    // Update sky and ground colors for night mode
    if (night_mode) {
        sky_color = lv_color_make(0, 50, 100);      // Dark blue for night sky
        ground_color = lv_color_make(60, 40, 20);   // Dark brown for night ground
    } else {
        sky_color = lv_color_make(0, 150, 255);     // Bright blue for day sky
        ground_color = lv_color_make(139, 90, 43);  // Brown earth for day
    }
    
    // Update shadow color based on day/night mode
    if (shadow_light) {
        lv_obj_set_style_shadow_color(shadow_light, get_primary_color(), 0);
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
