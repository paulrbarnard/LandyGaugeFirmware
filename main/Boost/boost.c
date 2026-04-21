/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

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

/* ── LVGL objects ──────────────────────────────────────────────────── */
static lv_obj_t *gauge_container = NULL;
static lv_obj_t *needle_obj = NULL;       /* custom-draw object for the needle   */
static lv_obj_t *center_cap = NULL;       /* center dot (on top of needle)       */
static lv_obj_t *units_label = NULL;

/* Cached needle angle (radians) — updated by update_needle(), read by draw cb */
static float needle_angle_rad = 0.0f;

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
 * @brief Draw callback for the needle object.
 *
 * Drawing the needle via a callback on a single object means both the
 * old and new positions are rendered inside the same dirty region,
 * eliminating the tearing caused by two separate lv_line bounding boxes
 * hitting different SPI transfer passes.
 */
static void needle_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    /* Needle pivot = gauge centre (adjusted for container padding) */
    int cx = (obj_coords.x1 + obj_coords.x2) / 2;
    int cy = (obj_coords.y1 + obj_coords.y2) / 2;

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = get_needle_color();
    dsc.width = 8;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    dsc.opa = LV_OPA_COVER;

    lv_point_t pts[2];
    pts[0].x = cx;
    pts[0].y = cy;
    pts[1].x = cx + (int)(cosf(needle_angle_rad) * NEEDLE_LENGTH);
    pts[1].y = cy + (int)(sinf(needle_angle_rad) * NEEDLE_LENGTH);

    lv_draw_line(draw_ctx, &dsc, &pts[0], &pts[1]);
}

/**
 * @brief Update the needle position (recalc angle, invalidate the single object)
 */
static void update_needle(void)
{
    if (!needle_obj) return;

    float angle_deg = psi_to_angle(current_boost_psi);
    needle_angle_rad = angle_deg * (float)M_PI / 180.0f;

    /* Single invalidation — old + new needle drawn in same buffer pass */
    lv_obj_invalidate(needle_obj);
}

/**
 * @brief Update units label
 */
static void update_units_label(void)
{
    if (!units_label) return;
    lv_label_set_text(units_label, use_bar_units ? "BAR" : "PSI");
}

/*******************************************************************************
 * Zone arc helper — smooth coloured band just OUTSIDE the tick tips (lv_arc)
 ******************************************************************************/
#define ZONE_ARC_WIDTH     4

static void draw_zone_arc(float start_angle, float end_angle,
                          lv_color_t color)
{
    int a_start = (int)(start_angle - GAUGE_START_ANGLE + 0.5f);
    int a_end   = (int)(end_angle   - GAUGE_START_ANGLE + 0.5f);

    /* Outer edge of the band aligns with the outer edge of the tick tips */
    int outer_r  = MAJOR_TICK_OUTER_R;                     /* 158 */
    int diameter = outer_r * 2;                             /* 316 */

    /* Tick-circle centre in parent content-area coordinates */
    int cx = GAUGE_CENTER_X - TICK_OFFSET;                  /* 160 */
    int cy = GAUGE_CENTER_Y - TICK_OFFSET;                  /* 160 */

    lv_obj_t *arc = lv_arc_create(gauge_container);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);

    /* Use absolute position so arc centre == tick centre
     * (avoids any theme-padding offset from LV_ALIGN_CENTER) */
    lv_obj_set_pos(arc, cx - outer_r, cy - outer_r);

    int rotation = (int)(GAUGE_START_ANGLE + 360.0f + 0.5f);  /* 135 */
    lv_arc_set_rotation(arc, rotation);

    lv_arc_set_bg_angles(arc, a_start, a_end);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ZONE_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    /* Hide the indicator arc (value-driven part) */
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
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
        ESP_LOGD(TAG, "gauge_container created");
    } else {
        lv_obj_clean(gauge_container);
        ESP_LOGD(TAG, "gauge_container cleaned for redraw");
    }
    
    int center_x = GAUGE_CENTER_X;
    int center_y = GAUGE_CENTER_Y;
    
    lv_color_t accent = get_accent_color(night_mode);
    
    // Draw tick marks and numbers — layout depends on selected unit
    // BAR: major every 0.5, minor every 0.1  (0 .. 2.0 BAR)
    // PSI: major every 5,   minor every 1    (0 .. 29 PSI)
    //
    // psi_to_angle() maps 0..2.0 (BAR) to the gauge sweep,
    // so we convert display values to BAR before computing angles.

    float warn_bar = BOOST_WARNING_PSI;            /* 1.5 BAR */

    if (use_bar_units) {
        /* ── BAR ticks ──────────────────────────────────────────────── */
        for (int tick = 0; tick <= 20; tick++) {
            float bar_val = tick * 0.1f;
            float angle_deg = psi_to_angle(bar_val);
            float angle_rad = angle_deg * M_PI / 180.0f;

            bool is_major = (tick % 5 == 0);

            int outer_r = is_major ? MAJOR_TICK_OUTER_R : MINOR_TICK_OUTER_R;
            int inner_r = is_major ? MAJOR_TICK_INNER_R : MINOR_TICK_INNER_R;
            int line_width = is_major ? 6 : 3;

            lv_color_t tick_color = (bar_val >= warn_bar) ? COLOR_WARNING : accent;

            lv_obj_t *tick_line = lv_line_create(gauge_container);
            lv_point_t *tick_points = malloc(2 * sizeof(lv_point_t));

            tick_points[0].x = center_x + cosf(angle_rad) * outer_r - TICK_OFFSET;
            tick_points[0].y = center_y + sinf(angle_rad) * outer_r - TICK_OFFSET;
            tick_points[1].x = center_x + cosf(angle_rad) * inner_r - TICK_OFFSET;
            tick_points[1].y = center_y + sinf(angle_rad) * inner_r - TICK_OFFSET;

            lv_line_set_points(tick_line, tick_points, 2);
            lv_obj_set_style_line_width(tick_line, line_width, 0);
            lv_obj_set_style_line_color(tick_line, tick_color, 0);
            lv_obj_set_style_line_rounded(tick_line, false, 0);

            if (is_major) {
                lv_obj_t *num_label = lv_label_create(gauge_container);
                char num_text[8];
                snprintf(num_text, sizeof(num_text), "%.1f", bar_val);
                lv_label_set_text(num_label, num_text);

                lv_color_t num_color = (bar_val >= warn_bar) ? COLOR_WARNING : accent;
                lv_obj_set_style_text_color(num_label, num_color, 0);
                lv_obj_set_style_text_font(num_label, &lv_font_montserrat_32, 0);

                int num_x = center_x + cosf(angle_rad) * NUMBER_RADIUS;
                int num_y = center_y + sinf(angle_rad) * NUMBER_RADIUS;
                lv_obj_align(num_label, LV_ALIGN_CENTER,
                             num_x - center_x,
                             num_y - center_y);
            }
        }
    } else {
        /* ── PSI ticks ──────────────────────────────────────────────── */
        int psi_max = (int)(BOOST_MAX_PSI * BAR_TO_PSI + 0.5f);   /* 29 */
        float warn_psi = warn_bar * BAR_TO_PSI;                    /* ~21.8 */

        for (int psi = 0; psi <= psi_max; psi++) {
            float bar_val = (float)psi * PSI_TO_BAR;
            float angle_deg = psi_to_angle(bar_val);
            float angle_rad = angle_deg * M_PI / 180.0f;

            bool is_major = (psi % 5 == 0);

            int outer_r = is_major ? MAJOR_TICK_OUTER_R : MINOR_TICK_OUTER_R;
            int inner_r = is_major ? MAJOR_TICK_INNER_R : MINOR_TICK_INNER_R;
            int line_width = is_major ? 6 : 3;

            lv_color_t tick_color = ((float)psi >= warn_psi) ? COLOR_WARNING : accent;

            lv_obj_t *tick_line = lv_line_create(gauge_container);
            lv_point_t *tick_points = malloc(2 * sizeof(lv_point_t));

            tick_points[0].x = center_x + cosf(angle_rad) * outer_r - TICK_OFFSET;
            tick_points[0].y = center_y + sinf(angle_rad) * outer_r - TICK_OFFSET;
            tick_points[1].x = center_x + cosf(angle_rad) * inner_r - TICK_OFFSET;
            tick_points[1].y = center_y + sinf(angle_rad) * inner_r - TICK_OFFSET;

            lv_line_set_points(tick_line, tick_points, 2);
            lv_obj_set_style_line_width(tick_line, line_width, 0);
            lv_obj_set_style_line_color(tick_line, tick_color, 0);
            lv_obj_set_style_line_rounded(tick_line, false, 0);

            if (is_major) {
                lv_obj_t *num_label = lv_label_create(gauge_container);
                char num_text[8];
                snprintf(num_text, sizeof(num_text), "%d", psi);
                lv_label_set_text(num_label, num_text);

                lv_color_t num_color = ((float)psi >= warn_psi) ? COLOR_WARNING : accent;
                lv_obj_set_style_text_color(num_label, num_color, 0);
                lv_obj_set_style_text_font(num_label, &lv_font_montserrat_32, 0);

                int num_x = center_x + cosf(angle_rad) * NUMBER_RADIUS;
                int num_y = center_y + sinf(angle_rad) * NUMBER_RADIUS;
                lv_obj_align(num_label, LV_ALIGN_CENTER,
                             num_x - center_x,
                             num_y - center_y);
            }
        }
    }

    /* ── Warning zone arc on outside of tick marks ─────────────────── */
    {
        float a_warn = psi_to_angle(BOOST_WARNING_PSI);
        float a_max  = psi_to_angle(BOOST_MAX_PSI);
        draw_zone_arc(a_warn, a_max, COLOR_WARNING);
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
    
    ESP_LOGD(TAG, "Gauge face drawn");
}

/**
 * @brief Create the needle (custom draw object) and center cap
 */
static void create_needle(void)
{
    /* Needle: a transparent square centred on the gauge that draws the
       needle line via a draw callback.  Using one object keeps the old
       and new needle in a single dirty region, preventing tearing. */
    needle_obj = lv_obj_create(gauge_container);
    lv_obj_set_size(needle_obj, NEEDLE_LENGTH * 2 + 20, NEEDLE_LENGTH * 2 + 20);
    lv_obj_center(needle_obj);
    lv_obj_set_style_bg_opa(needle_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(needle_obj, 0, 0);
    lv_obj_set_style_pad_all(needle_obj, 0, 0);
    lv_obj_clear_flag(needle_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(needle_obj, needle_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    update_needle();
    
    // Create center cap (raised button appearance)
    center_cap = lv_obj_create(gauge_container);
    lv_obj_set_size(center_cap, CENTER_DOT_SIZE, CENTER_DOT_SIZE);
    lv_obj_set_style_radius(center_cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center_cap, 0, 0);
    
    // Gradient effect - dark grey to black
    lv_obj_set_style_bg_color(center_cap, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_grad_color(center_cap, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_grad_dir(center_cap, LV_GRAD_DIR_HOR, 0);
    
    // Subtle highlight shadow
    lv_obj_set_style_shadow_width(center_cap, 10, 0);
    lv_obj_set_style_shadow_opa(center_cap, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(center_cap, get_accent_color(night_mode), 0);
    lv_obj_set_style_shadow_ofs_x(center_cap, -3, 0);
    lv_obj_set_style_shadow_ofs_y(center_cap, -3, 0);
    
    lv_obj_align(center_cap, LV_ALIGN_CENTER, 0, 0);
    
    ESP_LOGD(TAG, "Needle (draw-cb) and center cap created");
}

void boost_init(void)
{
    ESP_LOGD(TAG, "Initializing boost gauge");
    
    current_boost_psi = 0.0f;
    
    draw_gauge_face();
    create_needle();
    
    ESP_LOGD(TAG, "Boost gauge initialized");
}

/* Minimum change (BAR) before triggering a needle redraw.
   Prevents constant invalidation from starving LVGL input processing. */
#define BOOST_REDRAW_THRESHOLD  0.005f

void boost_set_value(float psi)
{
    // Clamp to valid range
    if (psi < BOOST_MIN_PSI) psi = BOOST_MIN_PSI;
    if (psi > BOOST_MAX_PSI) psi = BOOST_MAX_PSI;
    
    // Skip redraw if value hasn't changed enough
    if (fabsf(psi - current_boost_psi) < BOOST_REDRAW_THRESHOLD) {
        return;
    }
    
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
    ESP_LOGD(TAG, "Setting %s mode", night_mode ? "night" : "day");
    
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
    ESP_LOGD(TAG, "Cleaning up boost gauge");
    
    if (gauge_container) {
        lv_obj_del(gauge_container);
        gauge_container = NULL;
        needle_obj = NULL;
        center_cap = NULL;
        units_label = NULL;
    }
    
    // ESP_LOGD(TAG, "Boost gauge cleanup complete");
}

void boost_set_units_bar(bool use_bar)
{
    if (use_bar_units == use_bar) return;
    
    use_bar_units = use_bar;
    /* Redraw entire gauge with correct scale (only if already created) */
    if (gauge_container) {
        draw_gauge_face();
        create_needle();
    }
    ESP_LOGD(TAG, "Units set to %s", use_bar_units ? "bar" : "psi");
}

void boost_toggle_units(void)
{
    use_bar_units = !use_bar_units;
    /* Redraw entire gauge with correct scale */
    draw_gauge_face();
    create_needle();
    ESP_LOGD(TAG, "Units toggled to %s", use_bar_units ? "bar" : "psi");

    /* Persist to NVS */
    extern void settings_save_boost_units(bool);
    settings_save_boost_units(use_bar_units);
}
