/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

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
#include "Settings/settings.h"
#include "IMU/imu_attitude.h"
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

// Zero-offset: subtracted from raw IMU angle so current position reads 0°
static float tilt_offset = 0.0f;

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
#include "sd_images.h"

// LVGL objects
static lv_obj_t *tilt_gauge = NULL;
static lv_obj_t *tilt_img = NULL;            // Static vehicle image
static lv_obj_t *horizon_line_obj = NULL;    // Rotating horizon line (stays level with earth)
static lv_obj_t *left_scale_obj = NULL;      // Left side scale (static)
static lv_obj_t *right_scale_obj = NULL;     // Right side scale (static)

// Previous horizon line endpoints — used to compute targeted invalidation area
static lv_point_t prev_line_p0 = {0, 0};
static lv_point_t prev_line_p1 = {0, 0};
static bool       prev_line_valid = false;

// Pre-computed scale tick geometry (static — computed once at init)
#define SCALE_TICK_COUNT 4
typedef struct {
    lv_point_t upper_tick[2];   // Outer tick line endpoints
    lv_point_t lower_tick[2];   // Mirror tick (0 if tilt_val==0)
    lv_area_t  upper_label;     // Label bounding box
    lv_area_t  lower_label;     // Mirror label bounding box
    int        value;           // Tick value (0, 15, 30, 45)
    int        tick_len;        // Tick length
} scale_tick_t;

static scale_tick_t left_ticks[SCALE_TICK_COUNT];
static scale_tick_t right_ticks[SCALE_TICK_COUNT];
static lv_point_t   left_arc_center;
static lv_point_t   right_arc_center;

/**
 * @brief Pre-compute tick geometry for one side of the scale.
 *        Called once during tilt_init() — no trig at draw time.
 */
static void precompute_ticks(scale_tick_t *ticks, int32_t cx, int32_t cy,
                             int32_t radius, bool left_side)
{
    int tick_values[] = {0, 15, 30, 45};
    for (int i = 0; i < SCALE_TICK_COUNT; i++) {
        int tv = tick_values[i];
        ticks[i].value = tv;
        ticks[i].tick_len = (tv == 0) ? 15 : ((tv == 30) ? 12 : 8);
        int tl = ticks[i].tick_len;

        float upper_deg = left_side ? (180.0f - tv) : (float)(-tv);
        float upper_rad = upper_deg * (float)M_PI / 180.0f;
        float cu = cosf(upper_rad), su = sinf(upper_rad);

        ticks[i].upper_tick[0].x = cx + (int32_t)(radius * cu);
        ticks[i].upper_tick[0].y = cy + (int32_t)(radius * su);
        ticks[i].upper_tick[1].x = cx + (int32_t)((radius + tl) * cu);
        ticks[i].upper_tick[1].y = cy + (int32_t)((radius + tl) * su);

        if (tv > 0) {
            float lower_deg = left_side ? (180.0f + tv) : (float)tv;
            float lower_rad = lower_deg * (float)M_PI / 180.0f;
            float cl = cosf(lower_rad), sl = sinf(lower_rad);

            ticks[i].lower_tick[0].x = cx + (int32_t)(radius * cl);
            ticks[i].lower_tick[0].y = cy + (int32_t)(radius * sl);
            ticks[i].lower_tick[1].x = cx + (int32_t)((radius + tl) * cl);
            ticks[i].lower_tick[1].y = cy + (int32_t)((radius + tl) * sl);

            // Upper label
            ticks[i].upper_label.x1 = cx + (int32_t)((radius + 20) * cu) - 12;
            ticks[i].upper_label.y1 = cy + (int32_t)((radius + 20) * su) - 8;
            ticks[i].upper_label.x2 = ticks[i].upper_label.x1 + 24;
            ticks[i].upper_label.y2 = ticks[i].upper_label.y1 + 16;

            // Lower label
            ticks[i].lower_label.x1 = cx + (int32_t)((radius + 20) * cl) - 12;
            ticks[i].lower_label.y1 = cy + (int32_t)((radius + 20) * sl) - 8;
            ticks[i].lower_label.x2 = ticks[i].lower_label.x1 + 24;
            ticks[i].lower_label.y2 = ticks[i].lower_label.y1 + 16;
        }
    }
}

/**
 * @brief Draw callback for left side tilt scale (curved arc)
 * Uses pre-computed tick positions — no trig at draw time.
 */
static void draw_left_scale_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = scale_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    lv_draw_arc(draw_ctx, &arc_dsc, &left_arc_center, SCALE_RADIUS, 135, 225);

    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = scale_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa = LV_OPA_COVER;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = scale_line_color;
    label_dsc.opa = LV_OPA_COVER;

    for (int i = 0; i < SCALE_TICK_COUNT; i++) {
        scale_tick_t *t = &left_ticks[i];
        lv_draw_line(draw_ctx, &tick_dsc, &t->upper_tick[0], &t->upper_tick[1]);

        if (t->value > 0) {
            lv_draw_line(draw_ctx, &tick_dsc, &t->lower_tick[0], &t->lower_tick[1]);

            char label_text[12];
            snprintf(label_text, sizeof(label_text), "%d", t->value);
            lv_draw_label(draw_ctx, &label_dsc, &t->upper_label, label_text, NULL);
            lv_draw_label(draw_ctx, &label_dsc, &t->lower_label, label_text, NULL);
        }
    }
}

/**
 * @brief Draw callback for right side tilt scale (curved arc)
 * Uses pre-computed tick positions — no trig at draw time.
 */
static void draw_right_scale_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = scale_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    lv_draw_arc(draw_ctx, &arc_dsc, &right_arc_center, SCALE_RADIUS, 315, 405);

    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = scale_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa = LV_OPA_COVER;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = scale_line_color;
    label_dsc.opa = LV_OPA_COVER;

    for (int i = 0; i < SCALE_TICK_COUNT; i++) {
        scale_tick_t *t = &right_ticks[i];
        lv_draw_line(draw_ctx, &tick_dsc, &t->upper_tick[0], &t->upper_tick[1]);

        if (t->value > 0) {
            lv_draw_line(draw_ctx, &tick_dsc, &t->lower_tick[0], &t->lower_tick[1]);

            char label_text[12];
            snprintf(label_text, sizeof(label_text), "%d", t->value);
            lv_draw_label(draw_ctx, &label_dsc, &t->upper_label, label_text, NULL);
            lv_draw_label(draw_ctx, &label_dsc, &t->lower_label, label_text, NULL);
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
    lv_img_set_src(tilt_img, sd_images_get_rear(night_mode));
    
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

    // Pre-compute static scale tick geometry (no trig needed at draw time)
    // Force layout so lv_obj_get_coords returns real screen positions
    lv_obj_update_layout(tilt_gauge);
    {
        lv_area_t lc;
        lv_obj_get_coords(left_scale_obj, &lc);
        int32_t lcx = (lc.x1 + lc.x2) / 2, lcy = (lc.y1 + lc.y2) / 2;
        left_arc_center.x = lcx;
        left_arc_center.y = lcy;
        precompute_ticks(left_ticks, lcx, lcy, SCALE_RADIUS, true);

        lv_area_t rc;
        lv_obj_get_coords(right_scale_obj, &rc);
        int32_t rcx = (rc.x1 + rc.x2) / 2, rcy = (rc.y1 + rc.y2) / 2;
        right_arc_center.x = rcx;
        right_arc_center.y = rcy;
        precompute_ticks(right_ticks, rcx, rcy, SCALE_RADIUS, false);
    }
    
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

void tilt_zero_offset(void) {
    // Remove any previous offset so imu_get_roll() is read against true zero
    tilt_offset = 0.0f;
    // Capture the current raw angle as the new zero reference
    tilt_offset = imu_get_roll();
    ESP_LOGI(TAG, "Tilt zeroed: offset = %.1f°", tilt_offset);
    settings_save_tilt_offset(tilt_offset);
    // Force immediate redraw at 0°
    current_tilt_angle = 999.0f;  // ensure threshold triggers
    tilt_set_angle(imu_get_roll());
}

void tilt_set_offset(float offset_deg) {
    tilt_offset = offset_deg;
    ESP_LOGI(TAG, "Tilt offset restored: %.1f°", tilt_offset);
}

void tilt_set_angle(float angle_degrees) {
    // Apply zero-offset so calibrated position reads 0°
    float corrected = angle_degrees - tilt_offset;

    // Skip redraw if the angle hasn't changed enough
    float delta = fabsf(corrected - current_tilt_angle);
    if (delta < TILT_REDRAW_THRESHOLD) {
        return;  // No meaningful change — don't invalidate
    }

    // Update the stored tilt angle
    current_tilt_angle = corrected;
    
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
    
    // Targeted invalidation: only mark the old + new line bounding boxes
    // instead of the full 280x280 horizon_line_obj area.  This avoids
    // LVGL redrawing the overlapping image and scale objects.
    if (horizon_line_obj) {
        lv_area_t obj_coords;
        lv_obj_get_coords(horizon_line_obj, &obj_coords);
        int32_t cx = (obj_coords.x1 + obj_coords.x2) / 2;
        int32_t cy = (obj_coords.y1 + obj_coords.y2) / 2;
        int32_t half = SCALE_RADIUS - 2;
        float rad = current_tilt_angle * (float)M_PI / 180.0f;
        float ca = cosf(rad), sa = sinf(rad);
        lv_point_t p0 = { cx + (int32_t)(-half * ca), cy + (int32_t)(-half * sa) };
        lv_point_t p1 = { cx + (int32_t)( half * ca), cy + (int32_t)( half * sa) };

        // Build a tight rectangle around previous + new line positions
        #define LINE_PAD 4  // extra pixels for line width + anti-alias
        lv_area_t dirty;
        if (prev_line_valid) {
            dirty.x1 = LV_MIN(LV_MIN(prev_line_p0.x, prev_line_p1.x), LV_MIN(p0.x, p1.x)) - LINE_PAD;
            dirty.y1 = LV_MIN(LV_MIN(prev_line_p0.y, prev_line_p1.y), LV_MIN(p0.y, p1.y)) - LINE_PAD;
            dirty.x2 = LV_MAX(LV_MAX(prev_line_p0.x, prev_line_p1.x), LV_MAX(p0.x, p1.x)) + LINE_PAD;
            dirty.y2 = LV_MAX(LV_MAX(prev_line_p0.y, prev_line_p1.y), LV_MAX(p0.y, p1.y)) + LINE_PAD;
        } else {
            dirty.x1 = LV_MIN(p0.x, p1.x) - LINE_PAD;
            dirty.y1 = LV_MIN(p0.y, p1.y) - LINE_PAD;
            dirty.x2 = LV_MAX(p0.x, p1.x) + LINE_PAD;
            dirty.y2 = LV_MAX(p0.y, p1.y) + LINE_PAD;
        }
        lv_obj_invalidate_area(horizon_line_obj, &dirty);
        prev_line_p0 = p0;
        prev_line_p1 = p1;
        prev_line_valid = true;
        #undef LINE_PAD
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
            lv_img_set_src(tilt_img, sd_images_get_rear(night_mode));
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
