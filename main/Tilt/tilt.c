/**
 * @file tilt.c
 * @brief Tilt gauge display implementation using LVGL
 *        Displays a static rear-view vehicle image with a rotating horizon line
 *        that stays level with the earth regardless of vehicle tilt
 */

#include "tilt.h"
#include "lvgl.h"
#include "LVGL_Driver/style.h"
#include "WarningBeep/warning_beep.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "TILT";

// Display parameters
#define TILT_SIZE DISP_W  // Main instrument size (360)
#define TILT_CENTER_X (DISP_W / 2)
#define TILT_CENTER_Y (DISP_H / 2)

// Image dimensions for pivot calculation (rear_110_235 is 204x235)
#define REAR_IMG_WIDTH 204
#define REAR_IMG_HEIGHT 235

// Scale parameters
#define SCALE_RADIUS (TILT_SIZE / 2 - 38)  // Inset from edge to leave room for outer ticks/labels
#define SCALE_MAX_ANGLE 45  // Maximum tilt angle shown on scale

// Warning thresholds (same as roll thresholds in horizon gauge)
#define TILT_YELLOW_THRESHOLD 30.0f
#define TILT_RED_THRESHOLD 35.0f

// Display mode - can be changed at runtime
static bool night_mode = false;

// Current tilt angle (scales rotate by this amount)
static float current_tilt_angle = 0.0f;

// Scale line color (follows accent color or warning color)
static lv_color_t scale_line_color;

// Warning state tracking
typedef enum {
    WARNING_NONE,
    WARNING_YELLOW,
    WARNING_RED
} warning_level_t;
static warning_level_t tilt_warning_level = WARNING_NONE;
static warning_level_t current_tilt_audio_level = WARNING_NONE;

// Declare the external images
LV_IMG_DECLARE(rear_110_235);        // Day mode image
LV_IMG_DECLARE(rear_dark_110_235);   // Night mode image

// LVGL objects
static lv_obj_t *tilt_gauge = NULL;
static lv_obj_t *tilt_img = NULL;            // Static vehicle image
static lv_obj_t *horizon_line_obj = NULL;    // Rotating horizon line (stays level with earth)
static lv_obj_t *left_scale_obj = NULL;      // Left side scale (static)
static lv_obj_t *right_scale_obj = NULL;     // Right side scale (static)

/**
 * @brief Draw callback for left side tilt scale (curved arc)
 * Range: 0 to +45 degrees (vehicle tilting left)
 * The scale is static (fixed to display)
 */
static void draw_left_scale_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    int32_t radius = SCALE_RADIUS;
    
    // Scale is static - no tilt offset
    float tilt_offset = 0.0f;
    
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = scale_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    
    // Draw arc on left side (from 135° to 225° in LVGL coords, rotated by tilt)
    // LVGL angles: 0° at 3 o'clock, increases clockwise
    lv_point_t center_point;
    center_point.x = center_x;
    center_point.y = center_y;
    
    // Apply tilt offset to arc angles
    int16_t arc_start = 135 + (int16_t)tilt_offset;
    int16_t arc_end = 225 + (int16_t)tilt_offset;
    lv_draw_arc(draw_ctx, &arc_dsc, &center_point, radius, arc_start, arc_end);
    
    // Draw tick marks
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = scale_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa = LV_OPA_COVER;
    
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = scale_line_color;
    label_dsc.opa = LV_OPA_COVER;
    
    // Tick marks: 0, 15, 30, 45 (on left side, reading up from center)
    // 180° = horizontal left (0 tilt), 135° = 45° tilt up, 225° = 45° tilt down
    int tick_values[] = {0, 15, 30, 45};
    for (int i = 0; i < 4; i++) {
        int tilt_val = tick_values[i];
        
        // Upper tick (above horizontal) - apply tilt offset
        float upper_lvgl_angle = 180.0f - tilt_val + tilt_offset;
        float upper_angle_rad = upper_lvgl_angle * M_PI / 180.0f;
        
        int tick_len = (tilt_val == 0) ? 15 : ((tilt_val == 30) ? 12 : 8);
        
        // Upper tick - draw outward from arc
        int32_t x1 = center_x + (int32_t)(radius * cosf(upper_angle_rad));
        int32_t y1 = center_y + (int32_t)(radius * sinf(upper_angle_rad));
        int32_t x2 = center_x + (int32_t)((radius + tick_len) * cosf(upper_angle_rad));
        int32_t y2 = center_y + (int32_t)((radius + tick_len) * sinf(upper_angle_rad));
        
        lv_point_t tick_points[2];
        tick_points[0].x = x1;
        tick_points[0].y = y1;
        tick_points[1].x = x2;
        tick_points[1].y = y2;
        lv_draw_line(draw_ctx, &tick_dsc, &tick_points[0], &tick_points[1]);
        
        // Lower tick (below horizontal) - mirror with tilt offset
        if (tilt_val > 0) {
            float lower_lvgl_angle = 180.0f + tilt_val + tilt_offset;
            float lower_angle_rad = lower_lvgl_angle * M_PI / 180.0f;
            
            x1 = center_x + (int32_t)(radius * cosf(lower_angle_rad));
            y1 = center_y + (int32_t)(radius * sinf(lower_angle_rad));
            x2 = center_x + (int32_t)((radius + tick_len) * cosf(lower_angle_rad));
            y2 = center_y + (int32_t)((radius + tick_len) * sinf(lower_angle_rad));
            
            tick_points[0].x = x1;
            tick_points[0].y = y1;
            tick_points[1].x = x2;
            tick_points[1].y = y2;
            lv_draw_line(draw_ctx, &tick_dsc, &tick_points[0], &tick_points[1]);
        }
        
        // Add numeric labels for major ticks (15, 30, 45) - outside the arc
        if (tilt_val > 0) {
            char label_text[12];
            snprintf(label_text, sizeof(label_text), "%d", tilt_val);
            
            // Upper label - positioned outside arc
            lv_area_t label_area;
            label_area.x1 = center_x + (int32_t)((radius + 20) * cosf(upper_angle_rad)) - 12;
            label_area.y1 = center_y + (int32_t)((radius + 20) * sinf(upper_angle_rad)) - 8;
            label_area.x2 = label_area.x1 + 24;
            label_area.y2 = label_area.y1 + 16;
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
            
            // Lower label (mirror) - positioned outside arc
            float lower_angle_rad = (180.0f + tilt_val + tilt_offset) * M_PI / 180.0f;
            label_area.x1 = center_x + (int32_t)((radius + 20) * cosf(lower_angle_rad)) - 12;
            label_area.y1 = center_y + (int32_t)((radius + 20) * sinf(lower_angle_rad)) - 8;
            label_area.x2 = label_area.x1 + 24;
            label_area.y2 = label_area.y1 + 16;
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
        }
    }
}

/**
 * @brief Draw callback for right side tilt scale (curved arc)
 * Range: 0 to +45 degrees (vehicle tilting right)
 * The scale is static (fixed to display)
 */
static void draw_right_scale_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    int32_t center_x = (obj_coords.x1 + obj_coords.x2) / 2;
    int32_t center_y = (obj_coords.y1 + obj_coords.y2) / 2;
    int32_t radius = SCALE_RADIUS;
    
    // Scale is static - no tilt offset
    float tilt_offset = 0.0f;
    
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = scale_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    
    // Draw arc on right side (from 315° to 405° in LVGL coords, rotated by tilt)
    lv_point_t center_point;
    center_point.x = center_x;
    center_point.y = center_y;
    
    // Apply tilt offset to arc angles
    int16_t arc_start = 315 + (int16_t)tilt_offset;
    int16_t arc_end = 405 + (int16_t)tilt_offset;  // 405 = 45 + 360
    lv_draw_arc(draw_ctx, &arc_dsc, &center_point, radius, arc_start, arc_end);
    
    // Draw tick marks
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = scale_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa = LV_OPA_COVER;
    
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = scale_line_color;
    label_dsc.opa = LV_OPA_COVER;
    
    // Tick marks: 0, 15, 30, 45 (on right side)
    int tick_values[] = {0, 15, 30, 45};
    for (int i = 0; i < 4; i++) {
        int tilt_val = tick_values[i];
        
        // Upper tick (above horizontal) - apply tilt offset
        float upper_lvgl_angle = -tilt_val + tilt_offset;  // 0° to -45° (315°)
        float upper_angle_rad = upper_lvgl_angle * M_PI / 180.0f;
        
        int tick_len = (tilt_val == 0) ? 15 : ((tilt_val == 30) ? 12 : 8);
        
        // Upper tick - draw outward from arc
        int32_t x1 = center_x + (int32_t)(radius * cosf(upper_angle_rad));
        int32_t y1 = center_y + (int32_t)(radius * sinf(upper_angle_rad));
        int32_t x2 = center_x + (int32_t)((radius + tick_len) * cosf(upper_angle_rad));
        int32_t y2 = center_y + (int32_t)((radius + tick_len) * sinf(upper_angle_rad));
        
        lv_point_t tick_points[2];
        tick_points[0].x = x1;
        tick_points[0].y = y1;
        tick_points[1].x = x2;
        tick_points[1].y = y2;
        lv_draw_line(draw_ctx, &tick_dsc, &tick_points[0], &tick_points[1]);
        
        // Lower tick (below horizontal) - apply tilt offset
        if (tilt_val > 0) {
            float lower_lvgl_angle = tilt_val + tilt_offset;  // 0° to 45°
            float lower_angle_rad = lower_lvgl_angle * M_PI / 180.0f;
            
            x1 = center_x + (int32_t)(radius * cosf(lower_angle_rad));
            y1 = center_y + (int32_t)(radius * sinf(lower_angle_rad));
            x2 = center_x + (int32_t)((radius + tick_len) * cosf(lower_angle_rad));
            y2 = center_y + (int32_t)((radius + tick_len) * sinf(lower_angle_rad));
            
            tick_points[0].x = x1;
            tick_points[0].y = y1;
            tick_points[1].x = x2;
            tick_points[1].y = y2;
            lv_draw_line(draw_ctx, &tick_dsc, &tick_points[0], &tick_points[1]);
        }
        
        // Add numeric labels for major ticks (15, 30, 45) - outside the arc
        if (tilt_val > 0) {
            char label_text[12];
            snprintf(label_text, sizeof(label_text), "%d", tilt_val);
            
            // Upper label - positioned outside arc
            lv_area_t label_area;
            label_area.x1 = center_x + (int32_t)((radius + 20) * cosf(upper_angle_rad)) - 12;
            label_area.y1 = center_y + (int32_t)((radius + 20) * sinf(upper_angle_rad)) - 8;
            label_area.x2 = label_area.x1 + 24;
            label_area.y2 = label_area.y1 + 16;
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
            
            // Lower label (mirror) - positioned outside arc
            float lower_angle_rad_r = (tilt_val + tilt_offset) * M_PI / 180.0f;
            label_area.x1 = center_x + (int32_t)((radius + 20) * cosf(lower_angle_rad_r)) - 12;
            label_area.y1 = center_y + (int32_t)((radius + 20) * sinf(lower_angle_rad_r)) - 8;
            label_area.x2 = label_area.x1 + 24;
            label_area.y2 = label_area.y1 + 16;
            lv_draw_label(draw_ctx, &label_dsc, &label_area, label_text, NULL);
        }
    }
}

/**
 * @brief Draw callback for rotating horizontal reference line
 * This line rotates opposite to vehicle tilt to stay level with the earth
 */
static void draw_horizon_line_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    // Use display center (object may be smaller than full screen)
    int32_t center_x = obj_coords.x1 + (obj_coords.x2 - obj_coords.x1) / 2;
    int32_t center_y = obj_coords.y1 + (obj_coords.y2 - obj_coords.y1) / 2;
    
    // Line extends from left to right, stopping short of scale arcs
    int32_t line_half_len = SCALE_RADIUS - 2;
    
    // Apply tilt angle rotation to stay level with earth
    // When vehicle tilts right (positive angle), line rotates right to compensate
    float angle_rad = current_tilt_angle * M_PI / 180.0f;
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = scale_line_color;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    
    // Calculate rotated line endpoints
    lv_point_t line_points[2];
    line_points[0].x = center_x + (int32_t)(-line_half_len * cos_a);
    line_points[0].y = center_y + (int32_t)(-line_half_len * sin_a);
    line_points[1].x = center_x + (int32_t)(line_half_len * cos_a);
    line_points[1].y = center_y + (int32_t)(line_half_len * sin_a);
    
    lv_draw_line(draw_ctx, &line_dsc, &line_points[0], &line_points[1]);
}

void tilt_init(void) {
    ESP_LOGD(TAG, "Initializing tilt gauge");

    // Create the container for the tilt gauge
    tilt_gauge = lv_obj_create(lv_scr_act());
    lv_obj_set_size(tilt_gauge, 360, 360);  // Full display size
    lv_obj_center(tilt_gauge);
    
    // Set dark grey background to match other gauges (like horizon gauge)
    lv_obj_set_style_bg_color(tilt_gauge, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_bg_opa(tilt_gauge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tilt_gauge, 0, 0);
    lv_obj_set_style_radius(tilt_gauge, 0, 0);
    lv_obj_clear_flag(tilt_gauge, LV_OBJ_FLAG_SCROLLABLE);

    // Initialize scale line color
    scale_line_color = get_accent_color(night_mode);

    // Create static horizontal reference line (behind the image)
    // Sized to just cover the line's sweep area (2 * (SCALE_RADIUS-2) = 280)
    // so invalidation doesn't overlap the full-screen scale objects.
    horizon_line_obj = lv_obj_create(tilt_gauge);
    lv_obj_set_size(horizon_line_obj, 280, 280);
    lv_obj_center(horizon_line_obj);
    lv_obj_set_style_bg_opa(horizon_line_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(horizon_line_obj, 0, 0);
    lv_obj_clear_flag(horizon_line_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(horizon_line_obj, draw_horizon_line_cb, LV_EVENT_DRAW_MAIN, NULL);

    // Create the static rear-view vehicle image
    tilt_img = lv_img_create(tilt_gauge);
    
    // Set the image source based on night/day mode
    if (night_mode) {
        lv_img_set_src(tilt_img, &rear_dark_110_235);
    } else {
        lv_img_set_src(tilt_img, &rear_110_235);
    }
    
    // Center the image in the container (image does NOT rotate)
    lv_obj_align(tilt_img, LV_ALIGN_CENTER, 0, 0);
    
    // Create left side scale (this rotates based on tilt)
    left_scale_obj = lv_obj_create(tilt_gauge);
    lv_obj_set_size(left_scale_obj, TILT_SIZE, TILT_SIZE);
    lv_obj_center(left_scale_obj);
    lv_obj_set_style_bg_opa(left_scale_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_scale_obj, 0, 0);
    lv_obj_clear_flag(left_scale_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(left_scale_obj, draw_left_scale_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create right side scale
    right_scale_obj = lv_obj_create(tilt_gauge);
    lv_obj_set_size(right_scale_obj, TILT_SIZE, TILT_SIZE);
    lv_obj_center(right_scale_obj);
    lv_obj_set_style_bg_opa(right_scale_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_scale_obj, 0, 0);
    lv_obj_clear_flag(right_scale_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(right_scale_obj, draw_right_scale_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Create the shadow overlay effects (same as other gauges)
    create_gauge_shadows(tilt_gauge, night_mode);
    
    ESP_LOGD(TAG, "Tilt gauge initialized with static image/scales and rotating horizon line");
}

void tilt_set_visible(bool visible) {
    if (tilt_gauge) {
        if (visible) {
            lv_obj_clear_flag(tilt_gauge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tilt_gauge, LV_OBJ_FLAG_HIDDEN);
            // Stop warning audio when gauge is hidden
            warning_beep_start(WARNING_LEVEL_NONE);
            current_tilt_audio_level = WARNING_NONE;
        }
    }
}

/* Minimum angle change (degrees) before triggering a redraw.
   Prevents constant invalidation from starving LVGL input processing. */
#define TILT_REDRAW_THRESHOLD  1.0f

void tilt_set_angle(float angle_degrees) {
    // Skip redraw if the angle hasn't changed enough
    float delta = fabsf(angle_degrees - current_tilt_angle);
    if (delta < TILT_REDRAW_THRESHOLD) {
        return;  // No meaningful change — don't invalidate
    }

    // Update the stored tilt angle
    current_tilt_angle = angle_degrees;
    
    // Calculate warning level based on absolute tilt
    float abs_tilt = fabsf(current_tilt_angle);
    warning_level_t new_warning;
    
    if (abs_tilt >= TILT_RED_THRESHOLD) {
        new_warning = WARNING_RED;
    } else if (abs_tilt >= TILT_YELLOW_THRESHOLD) {
        new_warning = WARNING_YELLOW;
    } else {
        new_warning = WARNING_NONE;
    }
    
    // Update warning color if changed
    if (new_warning != tilt_warning_level) {
        tilt_warning_level = new_warning;
        
        // Update scale line color based on warning
        if (tilt_warning_level == WARNING_RED) {
            scale_line_color = lv_color_make(255, 0, 0);  // Red
            ESP_LOGW(TAG, "TILT RED WARNING: tilt=%.1f", abs_tilt);
        } else if (tilt_warning_level == WARNING_YELLOW) {
            scale_line_color = lv_color_make(255, 255, 0);  // Yellow
            ESP_LOGW(TAG, "TILT YELLOW WARNING: tilt=%.1f", abs_tilt);
        } else {
            scale_line_color = get_accent_color(night_mode);
        }
        
        // Invalidate scales to redraw with new color
        if (left_scale_obj) {
            lv_obj_invalidate(left_scale_obj);
        }
        if (right_scale_obj) {
            lv_obj_invalidate(right_scale_obj);
        }
    }
    
    // Update warning audio (beep + MP3) if changed
    if (new_warning != current_tilt_audio_level) {
        current_tilt_audio_level = new_warning;
        
        if (current_tilt_audio_level == WARNING_RED) {
            warning_beep_start(WARNING_LEVEL_RED);
            ESP_LOGI(TAG, "Tilt DANGER warning started (DangerRoll.mp3)");
        } else if (current_tilt_audio_level == WARNING_YELLOW) {
            warning_beep_start(WARNING_LEVEL_YELLOW);
            ESP_LOGI(TAG, "Tilt WARNING started (WarningRoll.mp3)");
        } else {
            warning_beep_start(WARNING_LEVEL_NONE);  // Stop audio
            ESP_LOGI(TAG, "Tilt warning audio stopped");
        }
    }
    
    // Invalidate horizon line so it redraws with new rotation and color
    if (horizon_line_obj) {
        lv_obj_invalidate(horizon_line_obj);
    }
    
    ESP_LOGD(TAG, "Tilt angle set to %.1f degrees (horizon line rotated)", angle_degrees);
}

void tilt_set_night_mode(bool night) {
    if (night_mode != night) {
        night_mode = night;
        
        // Update scale line color (respect current warning state)
        if (tilt_warning_level == WARNING_RED) {
            scale_line_color = lv_color_make(255, 0, 0);  // Red
        } else if (tilt_warning_level == WARNING_YELLOW) {
            scale_line_color = lv_color_make(255, 255, 0);  // Yellow
        } else {
            scale_line_color = get_accent_color(night_mode);
        }
        
        if (tilt_img) {
            // Update the image source based on the new mode
            if (night_mode) {
                lv_img_set_src(tilt_img, &rear_dark_110_235);
            } else {
                lv_img_set_src(tilt_img, &rear_110_235);
            }
        }
        
        // Invalidate horizon line and scales to redraw with new colors
        if (horizon_line_obj) {
            lv_obj_invalidate(horizon_line_obj);
        }
        if (left_scale_obj) {
            lv_obj_invalidate(left_scale_obj);
        }
        if (right_scale_obj) {
            lv_obj_invalidate(right_scale_obj);
        }
        
        // Recreate shadows with new mode colors
        // First remove old shadow children (shadows are added after the scales)
        if (tilt_gauge) {
            uint32_t child_count = lv_obj_get_child_cnt(tilt_gauge);
            // Keep: horizon_line (0), image (1), left_scale (2), right_scale (3) - delete shadows (4+)
            while (child_count > 4) {
                lv_obj_del(lv_obj_get_child(tilt_gauge, child_count - 1));
                child_count--;
            }
            create_gauge_shadows(tilt_gauge, night_mode);
        }
        
        ESP_LOGD(TAG, "Tilt gauge switched to %s mode", night_mode ? "night" : "day");
    }
}

void tilt_cleanup(void)
{
    ESP_LOGD(TAG, "Cleaning up tilt gauge");

    /* Stop any active warning audio */
    if (current_tilt_audio_level != WARNING_NONE) {
        warning_beep_start(WARNING_LEVEL_NONE);
        current_tilt_audio_level = WARNING_NONE;
    }
    tilt_warning_level = WARNING_NONE;

    if (tilt_gauge) {
        lv_obj_del(tilt_gauge);
        tilt_gauge = NULL;
        tilt_img = NULL;
        horizon_line_obj = NULL;
        left_scale_obj = NULL;
        right_scale_obj = NULL;
    }

    // ESP_LOGD(TAG, "Tilt gauge cleanup complete");
}
