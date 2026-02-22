/**
 * @file egt.c
 * @brief Exhaust Gas Temperature gauge display implementation using LVGL
 *
 * Visual style matches the boost gauge.
 * Range: 0–900°C (0–1650°F)
 * Yellow warning zone above 680°C, red danger zone above 750°C.
 * Internally stores temperature in °C, converts for display when in °F mode.
 */

#include "egt.h"
#include "esp_log.h"
#include "LVGL_Driver/style.h"
#include "PCM5101.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "EGT";

/* ── Display mode ────────────────────────────────────────────────────────── */
static bool night_mode = true;
static bool use_celsius = true;

/* ── Current EGT value (always stored in °C) ─────────────────────────── */
static float current_egt_c = 0.0f;

/* ── Temperature constants (°C) ──────────────────────────────────────── */
#define EGT_MIN_C          0.0f
#define EGT_MAX_C          900.0f
#define EGT_WARNING_C      680.0f       /* Yellow zone start */
#define EGT_DANGER_C       750.0f       /* Red zone start */

/* ── °F equivalents ──────────────────────────────────────────────────── */
#define C_TO_F(c)           ((c) * 9.0f / 5.0f + 32.0f)
#define EGT_MIN_F           C_TO_F(EGT_MIN_C)        /* 32 */
#define EGT_MAX_F           C_TO_F(EGT_MAX_C)        /* 1652 */
#define EGT_WARNING_F       C_TO_F(EGT_WARNING_C)    /* 1256 */
#define EGT_DANGER_F        C_TO_F(EGT_DANGER_C)     /* 1382 */

/* ── Gauge geometry (matches boost) ──────────────────────────────────── */
#define GAUGE_SIZE          360
#define GAUGE_CENTER_X      (GAUGE_SIZE / 2)
#define GAUGE_CENTER_Y      (GAUGE_SIZE / 2)

#define GAUGE_START_ANGLE   -225.0f     /* 7 o'clock position */
#define GAUGE_END_ANGLE     45.0f       /* 5 o'clock position */
#define GAUGE_SWEEP         (GAUGE_END_ANGLE - GAUGE_START_ANGLE)   /* 270° */

#define TICK_OFFSET         20
#define MAJOR_TICK_OUTER_R  158
#define MAJOR_TICK_INNER_R  135
#define MINOR_TICK_OUTER_R  158
#define MINOR_TICK_INNER_R  145
#define NUMBER_RADIUS       110
#define NEEDLE_LENGTH       120
#define CENTER_DOT_SIZE     50

/* ── Warning / danger colours ────────────────────────────────────────── */
#define COLOR_WARNING       lv_color_make(255, 200, 0)      /* Yellow */
#define COLOR_DANGER        lv_color_make(255, 60, 0)       /* Red/orange */

/* ── Redraw threshold (°C) — avoids starving touch input ─────────── */
#define EGT_REDRAW_THRESHOLD  1.0f

/* ── MP3 one-shot flags for warning / danger alerts ──────────────── */
static bool egt_warn_mp3_played  = false;   /* egtwar.mp3  at 680°C */
static bool egt_danger_mp3_played = false;  /* egtdang.mp3 at 750°C */

/* ── LVGL objects ────────────────────────────────────────────────────── */
static lv_obj_t *gauge_container = NULL;
static lv_obj_t *needle_obj = NULL;       /* custom-draw object for the needle   */
static lv_obj_t *center_cap = NULL;       /* center dot (on top of needle)       */
static lv_obj_t *units_label = NULL;

/* Cached needle angle (radians) — updated by update_needle(), read by draw cb */
static float needle_angle_rad = 0.0f;

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

/**
 * @brief Map a °C value to a gauge angle
 */
static float temp_to_angle(float temp_c)
{
    if (temp_c < EGT_MIN_C) temp_c = EGT_MIN_C;
    if (temp_c > EGT_MAX_C) temp_c = EGT_MAX_C;
    float ratio = (temp_c - EGT_MIN_C) / (EGT_MAX_C - EGT_MIN_C);
    return GAUGE_START_ANGLE + ratio * GAUGE_SWEEP;
}

/**
 * @brief Get needle colour based on current temperature
 */
static lv_color_t get_needle_color(void)
{
    if (current_egt_c >= EGT_DANGER_C)  return COLOR_DANGER;
    if (current_egt_c >= EGT_WARNING_C) return COLOR_WARNING;
    return get_accent_color(night_mode);
}

/**
 * @brief Get tick/number colour for a given temperature value (°C)
 */
static lv_color_t get_tick_color_for_temp(float temp_c)
{
    if (temp_c >= EGT_DANGER_C)  return COLOR_DANGER;
    if (temp_c >= EGT_WARNING_C) return COLOR_WARNING;
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
 * @brief Update needle position (recalc angle, invalidate the single object)
 */
static void update_needle(void)
{
    if (!needle_obj) return;

    float angle_deg = temp_to_angle(current_egt_c);
    needle_angle_rad = angle_deg * (float)M_PI / 180.0f;

    /* Single invalidation — old + new needle drawn in same buffer pass */
    lv_obj_invalidate(needle_obj);
}

/**
 * @brief Update units label text
 */
static void update_units_label(void)
{
    if (!units_label) return;
    lv_label_set_text(units_label, use_celsius ? "°C" : "°F");
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

/*******************************************************************************
 * Gauge face drawing
 ******************************************************************************/

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

    if (use_celsius) {
        /* ── °C ticks: major every 100°C, minor every 50°C, 0–900 ─── */
        for (int t = 0; t <= 900; t += 50) {
            float temp_c = (float)t;
            float angle_deg = temp_to_angle(temp_c);
            float angle_rad = angle_deg * (float)M_PI / 180.0f;

            bool is_major = (t % 100 == 0);
            int outer_r = is_major ? MAJOR_TICK_OUTER_R : MINOR_TICK_OUTER_R;
            int inner_r = is_major ? MAJOR_TICK_INNER_R : MINOR_TICK_INNER_R;
            int line_w  = is_major ? 6 : 3;

            lv_color_t tick_color = get_tick_color_for_temp(temp_c);

            lv_obj_t *tick_line = lv_line_create(gauge_container);
            lv_point_t *tp = malloc(2 * sizeof(lv_point_t));
            tp[0].x = center_x + cosf(angle_rad) * outer_r - TICK_OFFSET;
            tp[0].y = center_y + sinf(angle_rad) * outer_r - TICK_OFFSET;
            tp[1].x = center_x + cosf(angle_rad) * inner_r - TICK_OFFSET;
            tp[1].y = center_y + sinf(angle_rad) * inner_r - TICK_OFFSET;
            lv_line_set_points(tick_line, tp, 2);
            lv_obj_set_style_line_width(tick_line, line_w, 0);
            lv_obj_set_style_line_color(tick_line, tick_color, 0);
            lv_obj_set_style_line_rounded(tick_line, false, 0);

            if (is_major) {
                lv_obj_t *num = lv_label_create(gauge_container);
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", t);
                lv_label_set_text(num, buf);

                lv_obj_set_style_text_color(num, tick_color, 0);
                lv_obj_set_style_text_font(num, &lv_font_montserrat_16, 0);

                int nx = center_x + cosf(angle_rad) * NUMBER_RADIUS;
                int ny = center_y + sinf(angle_rad) * NUMBER_RADIUS;
                lv_obj_align(num, LV_ALIGN_CENTER,
                             nx - center_x, ny - center_y);
            }
        }
    } else {
        /* ── °F ticks: major every 200°F, minor every 100°F, 0–1650 ─ */
        int f_max = (int)(EGT_MAX_F + 0.5f);   /* 1652 → round to 1650 */
        f_max = 1650;                            /* Exact nice endpoint */

        for (int f = 0; f <= f_max; f += 100) {
            /* Convert display °F back to °C to get gauge angle */
            float temp_c = ((float)f - 32.0f) * 5.0f / 9.0f;
            float angle_deg = temp_to_angle(temp_c);
            float angle_rad = angle_deg * (float)M_PI / 180.0f;

            bool is_major = (f % 200 == 0);
            int outer_r = is_major ? MAJOR_TICK_OUTER_R : MINOR_TICK_OUTER_R;
            int inner_r = is_major ? MAJOR_TICK_INNER_R : MINOR_TICK_INNER_R;
            int line_w  = is_major ? 6 : 3;

            lv_color_t tick_color = get_tick_color_for_temp(temp_c);

            lv_obj_t *tick_line = lv_line_create(gauge_container);
            lv_point_t *tp = malloc(2 * sizeof(lv_point_t));
            tp[0].x = center_x + cosf(angle_rad) * outer_r - TICK_OFFSET;
            tp[0].y = center_y + sinf(angle_rad) * outer_r - TICK_OFFSET;
            tp[1].x = center_x + cosf(angle_rad) * inner_r - TICK_OFFSET;
            tp[1].y = center_y + sinf(angle_rad) * inner_r - TICK_OFFSET;
            lv_line_set_points(tick_line, tp, 2);
            lv_obj_set_style_line_width(tick_line, line_w, 0);
            lv_obj_set_style_line_color(tick_line, tick_color, 0);
            lv_obj_set_style_line_rounded(tick_line, false, 0);

            if (is_major) {
                lv_obj_t *num = lv_label_create(gauge_container);
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", f);
                lv_label_set_text(num, buf);

                lv_obj_set_style_text_color(num, tick_color, 0);
                /* Use 14pt font for larger °F numbers */
                lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);

                int nx = center_x + cosf(angle_rad) * NUMBER_RADIUS;
                int ny = center_y + sinf(angle_rad) * NUMBER_RADIUS;
                lv_obj_align(num, LV_ALIGN_CENTER,
                             nx - center_x, ny - center_y);
            }
        }
    }

    /* ── Zone arcs (yellow warning, red danger) outside tick marks ── */
    {
        float a_warn   = temp_to_angle(EGT_WARNING_C);
        float a_danger = temp_to_angle(EGT_DANGER_C);
        float a_max    = temp_to_angle(EGT_MAX_C);
        draw_zone_arc(a_warn, a_danger, COLOR_WARNING);
        draw_zone_arc(a_danger, a_max,  COLOR_DANGER);
    }

    /* Units label (°C or °F) */
    units_label = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(units_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(units_label, accent, 0);
    lv_obj_align(units_label, LV_ALIGN_CENTER, 0, 45);
    update_units_label();

    /* Title label */
    lv_obj_t *title = lv_label_create(gauge_container);
    lv_label_set_text(title, "EGT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, accent, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 65);

    /* Shadow overlay */
    create_gauge_shadows(gauge_container, night_mode);

    ESP_LOGD(TAG, "Gauge face drawn (%s mode, %s)",
             night_mode ? "night" : "day",
             use_celsius ? "°C" : "°F");
}

/*******************************************************************************
 * Needle & center cap
 ******************************************************************************/

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

    /* Center cap (same as boost) */
    center_cap = lv_obj_create(gauge_container);
    lv_obj_set_size(center_cap, CENTER_DOT_SIZE, CENTER_DOT_SIZE);
    lv_obj_set_style_radius(center_cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center_cap, 0, 0);
    lv_obj_set_style_bg_color(center_cap, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_grad_color(center_cap, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_grad_dir(center_cap, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_width(center_cap, 10, 0);
    lv_obj_set_style_shadow_opa(center_cap, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(center_cap, get_accent_color(night_mode), 0);
    lv_obj_set_style_shadow_ofs_x(center_cap, -3, 0);
    lv_obj_set_style_shadow_ofs_y(center_cap, -3, 0);
    lv_obj_align(center_cap, LV_ALIGN_CENTER, 0, 0);

    ESP_LOGD(TAG, "Needle (draw-cb) and center cap created");
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

void egt_init(void)
{
    ESP_LOGD(TAG, "Initializing EGT gauge");
    current_egt_c = 0.0f;
    draw_gauge_face();
    create_needle();
    ESP_LOGD(TAG, "EGT gauge initialized");
}

void egt_set_value(float temp_c)
{
    if (temp_c < EGT_MIN_C) temp_c = EGT_MIN_C;
    if (temp_c > EGT_MAX_C) temp_c = EGT_MAX_C;

    /* ── MP3 alerts (one-shot, reset when temp drops below threshold) ─ */
    if (temp_c >= EGT_DANGER_C) {
        if (!egt_danger_mp3_played) {
            egt_danger_mp3_played = true;
            Play_Music("/sdcard", "egtdang.mp3");
            ESP_LOGW(TAG, "EGT DANGER %.0f°C — playing egtdang.mp3", temp_c);
        }
    } else if (temp_c >= EGT_WARNING_C) {
        if (!egt_warn_mp3_played) {
            egt_warn_mp3_played = true;
            Play_Music("/sdcard", "egtwar.mp3");
            ESP_LOGW(TAG, "EGT WARNING %.0f°C — playing egtwar.mp3", temp_c);
        }
        egt_danger_mp3_played = false;  /* Reset danger so it can re-trigger */
    } else {
        egt_warn_mp3_played = false;
        egt_danger_mp3_played = false;
    }

    /* Skip redraw if value hasn't changed enough */
    if (fabsf(temp_c - current_egt_c) < EGT_REDRAW_THRESHOLD) {
        return;
    }

    current_egt_c = temp_c;
    update_needle();
}

float egt_get_value(void)
{
    return current_egt_c;
}

void egt_set_night_mode(bool is_night_mode)
{
    if (night_mode == is_night_mode) return;
    night_mode = is_night_mode;
    ESP_LOGD(TAG, "Setting %s mode", night_mode ? "night" : "day");
    draw_gauge_face();
    create_needle();
}

void egt_set_visible(bool visible)
{
    if (gauge_container) {
        if (visible) {
            lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void egt_cleanup(void)
{
    ESP_LOGD(TAG, "Cleaning up EGT gauge");
    if (gauge_container) {
        lv_obj_del(gauge_container);
        gauge_container = NULL;
        needle_obj = NULL;
        center_cap = NULL;
        units_label = NULL;
    }
    // ESP_LOGD(TAG, "EGT gauge cleanup complete");
}

void egt_set_units_celsius(bool celsius)
{
    if (use_celsius == celsius) return;
    use_celsius = celsius;
    /* Redraw gauge with correct scale (only if already created) */
    if (gauge_container) {
        draw_gauge_face();
        create_needle();
    }
    ESP_LOGD(TAG, "Units set to %s", use_celsius ? "°C" : "°F");
}

void egt_toggle_units(void)
{
    use_celsius = !use_celsius;
    draw_gauge_face();
    create_needle();
    ESP_LOGD(TAG, "Units toggled to %s", use_celsius ? "°C" : "°F");

    /* Persist to NVS */
    extern void settings_save_egt_units(bool);
    settings_save_egt_units(use_celsius);
}

bool egt_is_celsius(void)
{
    return use_celsius;
}
