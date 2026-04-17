/**
 * @file cooling.c
 * @brief Cooling status gauge implementation using LVGL
 *
 * Three-sector circular gauge:
 *   Top-left sector  — Fan Low (IN3):  fan icon, rotates when active
 *   Top-right sector — Fan High (IN4): fan icon, rotates when active
 *   Bottom sector    — Coolant (IN5):  radiator with water level
 *
 * Wading mode toggled via select button — controls OUT1, fans turn red.
 * Auto-switch triggered when any input activates.
 */

#include "cooling.h"
#include "esp_log.h"
#include "LVGL_Driver/style.h"
#include "expansion_board.h"
#include "mcp23017.h"
#include "PCM5101.h"
#include "imu_attitude.h"
#include "QMI8658/QMI8658.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "COOLING";

/* ── IMU-based coolant float filter ─────────────────────────────────── *
 *
 * The coolant low sensor is a float switch.  Braking, acceleration, or
 * significant tilt causes the fluid to slosh, giving false readings.
 *
 * Instead of a simple time delay we use the IMU:
 *   • "Settled" = tilt (pitch & roll) within limits AND lateral/longitudinal
 *     G-force deviation from static 1 g is small.
 *   • Only accumulate "low" time while the vehicle is settled.
 *   • Alarm confirmed after COOLANT_CONFIRM_MS of accumulated settled-low time.
 *   • If sensor reads OK while settled, clear after COOLANT_CLEAR_MS.
 *   • While unsettled the counters freeze — we simply don't trust the reading.
 */
#define COOLANT_CONFIRM_MS      3000   /* Settled-low time to confirm alarm      */
#define COOLANT_CLEAR_MS        1000   /* Settled-OK time to clear alarm         */
#define COOLANT_MAX_TILT_RATE   2.0f   /* Max degrees change per sample to be "settled" */
#define COOLANT_MAX_G_DEVIATION 0.15f  /* Max deviation from 1 g (in g units)    */

static uint32_t coolant_low_accum_ms  = 0;   /* Accumulated settled-low time     */
static uint32_t coolant_ok_accum_ms   = 0;   /* Accumulated settled-OK time      */
static bool     coolant_confirmed_low = false; /* Filtered/confirmed alarm state */
static bool     coolant_low_mp3_played = false; /* one-shot coolant-low MP3 flag */
static uint32_t coolant_last_update_ms = 0;

/* Previous pitch/roll for rate-of-change check */
static float prev_pitch = 0.0f;
static float prev_roll  = 0.0f;
static bool  prev_valid = false;

/**
 * @brief Check if vehicle is settled enough to trust the coolant float.
 *
 * Uses rate-of-change of pitch/roll (not absolute angle — mounting offset
 * would cause false "unsettled") and raw accelerometer magnitude.
 */
static bool vehicle_is_settled(void)
{
    float pitch = imu_get_pitch();
    float roll  = imu_get_roll();

    /* Check tilt stability: if pitch or roll changed more than threshold
       since last sample, vehicle is moving/bouncing */
    if (prev_valid) {
        float dp = fabsf(pitch - prev_pitch);
        float dr = fabsf(roll  - prev_roll);
        prev_pitch = pitch;
        prev_roll  = roll;
        if (dp > COOLANT_MAX_TILT_RATE || dr > COOLANT_MAX_TILT_RATE)
            return false;
    } else {
        prev_pitch = pitch;
        prev_roll  = roll;
        prev_valid = true;
        return false;  /* First sample — can't compute rate yet */
    }

    /* Check total G magnitude — should be close to 1.0 g when stationary.
       Significant braking/accel/cornering pushes it away from 1 g. */
    float g_mag = sqrtf(Accel.x * Accel.x + Accel.y * Accel.y + Accel.z * Accel.z);
    if (fabsf(g_mag - 1.0f) > COOLANT_MAX_G_DEVIATION)
        return false;

    return true;
}

/**
 * @brief Update the filtered coolant state.
 *
 * Called at ~10 Hz from cooling_update().  Returns the filtered coolant-OK
 * status (true = level OK, false = confirmed low).
 *
 * @param raw_low  true if the raw sensor says coolant is low right now
 * @param dt_ms    elapsed time since last call
 */
static bool coolant_filter_update(bool raw_low, uint32_t dt_ms)
{
    bool settled = vehicle_is_settled();

    if (settled) {
        if (raw_low) {
            /* Accumulate settled-low time, reset OK counter */
            coolant_low_accum_ms += dt_ms;
            coolant_ok_accum_ms = 0;

            if (!coolant_confirmed_low && coolant_low_accum_ms >= COOLANT_CONFIRM_MS) {
                coolant_confirmed_low = true;
                ESP_LOGW(TAG, "Coolant LOW confirmed after %lu ms settled-low",
                         (unsigned long)coolant_low_accum_ms);
                if (!coolant_low_mp3_played) {
                    coolant_low_mp3_played = true;
                    Play_Music("/sdcard", "coollow.mp3");
                    ESP_LOGW(TAG, "Playing coollow.mp3");
                }
            }
        } else {
            /* Sensor says OK — accumulate settled-OK time, reset low counter */
            coolant_ok_accum_ms += dt_ms;
            coolant_low_accum_ms = 0;

            if (coolant_confirmed_low && coolant_ok_accum_ms >= COOLANT_CLEAR_MS) {
                coolant_confirmed_low = false;
                coolant_low_mp3_played = false;
                ESP_LOGI(TAG, "Coolant OK — alarm cleared after %lu ms settled-OK",
                         (unsigned long)coolant_ok_accum_ms);
            }
        }
    }
    /* While unsettled: counters freeze — don't trust the reading either way */

    return !coolant_confirmed_low;  /* true = OK, false = low */
}

/* ── Display state ──────────────────────────────────────────────────── */
static bool night_mode = true;
static bool is_visible = false;
static bool wading_mode = false;

/* ── Manual fan override state ──────────────────────────────────────── */
static bool fan_low_override  = false;   /* Manual fan low via GPA1 */
static bool fan_high_override = false;   /* Manual fan high via GPA2 */
#define FAN_OVERRIDE_TIMEOUT_MS  (5UL * 60 * 1000)  /* 5 minutes */
static uint32_t fan_low_override_start  = 0;  /* Timestamp when override activated */
static uint32_t fan_high_override_start = 0;

/* ── Cached input states (updated each cooling_update call) ─────────── */
static bool fan_low_active  = false;
static bool fan_high_active = false;
static bool coolant_ok      = true;   /* true = level OK (IN5 high), false = low */

/* ── Coolant temperature overtemp tracking ───────────────────────────── */
#define COOLANT_TEMP_HIDE_C      60.0f   /* Hide reading at or below this */
#define COOLANT_TEMP_WARN_C     110.0f   /* Yellow warning threshold */
#define COOLANT_TEMP_DANGER_C   115.0f   /* Red danger + MP3 + alarm */
static bool coolant_overtemp = false;    /* true when ≥ DANGER threshold */
static bool overheat_mp3_played = false; /* one-shot MP3 flag */

/* ── Fan rotation animation ─────────────────────────────────────────── */
#define FAN_ROTATE_PERIOD_MS   30     /* Timer period for smooth rotation */
#define FAN_ROTATE_STEP_DEG    8      /* Degrees per tick — 8° × 33Hz ≈ 267°/s */
static lv_timer_t *fan_anim_timer = NULL;
static int16_t fan_low_angle  = 0;    /* 0-3599 (tenths of a degree for LVGL) */
static int16_t fan_high_angle = 0;

/* ── LVGL objects ────────────────────────────────────────────────────── */
static lv_obj_t *gauge_container = NULL;   /* 360×360 background circle */

/* Sector divider lines — removed for cleaner look */

/* Fan icons — drawn via custom draw callback on transparent canvas */
static lv_obj_t *fan_low_obj  = NULL;      /* Top-left fan icon */
static lv_obj_t *fan_high_obj = NULL;      /* Top-right fan icon */

/* Fan label */
static lv_obj_t *fan_low_label  = NULL;    /* "LOW" label */
static lv_obj_t *fan_high_label = NULL;    /* "HIGH" label */

/* Coolant icon — drawn via custom draw callback */
static lv_obj_t *coolant_obj = NULL;       /* Bottom sector radiator icon */

/* Wading mode indicator */
static lv_obj_t *wading_label = NULL;      /* "WADING" text (shown when active) */

/* Coolant temperature from analogue sender */
static lv_obj_t *coolant_temp_label = NULL; /* e.g. "85°C" overlaid on radiator */

/* Title */
static lv_obj_t *title_label = NULL;       /* "COOLING" title */

/* ── Colors ──────────────────────────────────────────────────────────── */
#define COLOR_RED       lv_color_make(255, 0, 0)
#define COLOR_SECONDARY lv_color_make(80, 80, 80)    /* Inactive / dim */

static lv_color_t get_fan_color(bool active, bool is_wading)
{
    if (is_wading)  return COLOR_RED;
    if (active)     return get_accent_color(night_mode);
    return COLOR_SECONDARY;
}

static lv_color_t get_coolant_color(bool ok)
{
    return ok ? get_accent_color(night_mode) : COLOR_RED;
}

/* ── Forward declarations ────────────────────────────────────────────── */
static void fan_draw_cb(lv_event_t *e);
static void coolant_draw_cb(lv_event_t *e);
static void fan_anim_timer_cb(lv_timer_t *timer);
static void draw_gauge_face(void);

/*******************************************************************************
 * Fan icon drawing — 6-blade fan drawn with lines from center
 ******************************************************************************/

/**
 * @brief Draw a fan icon (6 curved blades) at a given center with rotation.
 *
 * Uses thick rounded lines radiating from the center, with slight arc
 * offsets to suggest curved blades.
 */
static void draw_fan_icon(lv_draw_ctx_t *draw_ctx, int cx, int cy,
                          int radius, int16_t angle_deg10, lv_color_t color)
{
    float base_rad = (float)angle_deg10 / 10.0f * (float)M_PI / 180.0f;

    /* 6 blades, 60° apart */
    for (int i = 0; i < 6; i++) {
        float blade_rad = base_rad + (float)i * (float)M_PI / 3.0f;
        float cos_b = cosf(blade_rad);
        float sin_b = sinf(blade_rad);

        /* Blade body — thick line from near center to outer radius */
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = color;
        dsc.width = 10;
        dsc.round_start = 1;
        dsc.round_end   = 1;

        /* Inner start (small offset from center for hub look) */
        int inner_r = radius / 5;
        lv_point_t p0 = { cx + (int)(cos_b * inner_r),
                          cy + (int)(sin_b * inner_r) };
        lv_point_t p1 = { cx + (int)(cos_b * radius),
                          cy + (int)(sin_b * radius) };

        lv_draw_line(draw_ctx, &dsc, &p0, &p1);

        /* Blade tip — a shorter, slightly offset line for curvature effect */
        float tip_rad = blade_rad + 0.3f;  /* ~17° offset */
        lv_draw_line_dsc_t tip_dsc;
        lv_draw_line_dsc_init(&tip_dsc);
        tip_dsc.color = color;
        tip_dsc.width = 7;
        tip_dsc.round_start = 1;
        tip_dsc.round_end   = 1;

        int mid_r = radius * 2 / 3;
        lv_point_t t0 = { cx + (int)(cosf(tip_rad) * mid_r),
                          cy + (int)(sinf(tip_rad) * mid_r) };
        lv_point_t t1 = { cx + (int)(cosf(tip_rad) * radius),
                          cy + (int)(sinf(tip_rad) * radius) };
        lv_draw_line(draw_ctx, &tip_dsc, &t0, &t1);
    }

    /* Center hub circle */
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color  = color;
    arc_dsc.width  = 4;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle   = 360;
    lv_draw_arc(draw_ctx, &arc_dsc, &(lv_point_t){cx, cy}, radius / 4, 0, 360);
}

/**
 * @brief LVGL draw callback for fan icons.
 *
 * The draw callback user_data encodes which fan:
 *   user_data == (void*)0 → fan_low
 *   user_data == (void*)1 → fan_high
 */
static void fan_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);
    int fan_id = (int)(intptr_t)lv_event_get_user_data(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;

    bool active;
    int16_t angle;
    if (fan_id == 0) {
        active = fan_low_active;
        angle  = fan_low_angle;
    } else {
        active = fan_high_active;
        angle  = fan_high_angle;
    }

    lv_color_t color = get_fan_color(active, wading_mode);
    /* Rotate only when fan is active and not in wading mode */
    int16_t draw_angle = (active && !wading_mode) ? angle : 0;
    draw_fan_icon(draw_ctx, cx, cy, 50, draw_angle, color);
}

/*******************************************************************************
 * Coolant / Radiator icon drawing
 ******************************************************************************/

/**
 * @brief Draw a radiator icon with water level indicator.
 *
 * The radiator is drawn as a rectangle with vertical channel lines.
 * Water level is high (full) when coolant is OK, low when coolant is low.
 */
static void coolant_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = (coords.y1 + coords.y2) / 2;

    lv_color_t color = get_coolant_color(coolant_ok);

    /* Radiator body dimensions (80% of original) */
    int rad_w = 56;   /* half-width */
    int rad_h = 40;   /* half-height */

    /* Outer frame — 4 lines forming a rectangle */
    lv_draw_line_dsc_t frame_dsc;
    lv_draw_line_dsc_init(&frame_dsc);
    frame_dsc.color = color;
    frame_dsc.width = 3;
    frame_dsc.round_start = 0;
    frame_dsc.round_end   = 0;

    lv_point_t tl = { cx - rad_w, cy - rad_h };
    lv_point_t tr = { cx + rad_w, cy - rad_h };
    lv_point_t bl = { cx - rad_w, cy + rad_h };
    lv_point_t br = { cx + rad_w, cy + rad_h };

    lv_draw_line(draw_ctx, &frame_dsc, &tl, &tr);  /* top */
    lv_draw_line(draw_ctx, &frame_dsc, &bl, &br);  /* bottom */
    lv_draw_line(draw_ctx, &frame_dsc, &tl, &bl);  /* left */
    lv_draw_line(draw_ctx, &frame_dsc, &tr, &br);  /* right */

    /* Vertical channel lines inside the radiator */
    lv_draw_line_dsc_t chan_dsc;
    lv_draw_line_dsc_init(&chan_dsc);
    chan_dsc.color = color;
    chan_dsc.width = 2;

    int num_channels = 7;
    for (int i = 1; i < num_channels; i++) {
        int x = cx - rad_w + (2 * rad_w * i) / num_channels;
        lv_point_t ct = { x, cy - rad_h + 3 };
        lv_point_t cb = { x, cy + rad_h - 3 };
        lv_draw_line(draw_ctx, &chan_dsc, &ct, &cb);
    }

    /* Top/bottom tank caps (small horizontal lines extending beyond frame) */
    lv_draw_line_dsc_t cap_dsc;
    lv_draw_line_dsc_init(&cap_dsc);
    cap_dsc.color = color;
    cap_dsc.width = 5;
    cap_dsc.round_start = 1;
    cap_dsc.round_end   = 1;

    int cap_ext = 6;
    lv_point_t cap_tl = { cx - rad_w - cap_ext, cy - rad_h };
    lv_point_t cap_tr = { cx + rad_w + cap_ext, cy - rad_h };
    lv_draw_line(draw_ctx, &cap_dsc, &cap_tl, &cap_tr);

    lv_point_t cap_bl = { cx - rad_w - cap_ext, cy + rad_h };
    lv_point_t cap_br = { cx + rad_w + cap_ext, cy + rad_h };
    lv_draw_line(draw_ctx, &cap_dsc, &cap_bl, &cap_br);

    /* Inlet/outlet pipes (short vertical lines from caps) */
    lv_draw_line_dsc_t pipe_dsc;
    lv_draw_line_dsc_init(&pipe_dsc);
    pipe_dsc.color = color;
    pipe_dsc.width = 4;
    pipe_dsc.round_start = 1;
    pipe_dsc.round_end   = 1;

    /* Left pipe (top) */
    lv_point_t pipe_lt = { cx - rad_w / 2, cy - rad_h - 10 };
    lv_point_t pipe_lb = { cx - rad_w / 2, cy - rad_h };
    lv_draw_line(draw_ctx, &pipe_dsc, &pipe_lt, &pipe_lb);

    /* Right pipe (bottom) */
    lv_point_t pipe_rt = { cx + rad_w / 2, cy + rad_h };
    lv_point_t pipe_rb = { cx + rad_w / 2, cy + rad_h + 10 };
    lv_draw_line(draw_ctx, &pipe_dsc, &pipe_rt, &pipe_rb);

    /* Water level — filled rectangle at bottom of radiator body */
    /* When coolant OK: water fills to ~80% of radiator height */
    /* When coolant low: water fills only ~20% of radiator height */
    int water_fill_pct = coolant_ok ? 80 : 20;
    int water_top = cy + rad_h - (2 * rad_h * water_fill_pct / 100);
    int water_bot = cy + rad_h - 3;

    /* Draw water as horizontal lines filling the area */
    lv_draw_line_dsc_t water_dsc;
    lv_draw_line_dsc_init(&water_dsc);
    water_dsc.width = 2;

    /* Use blue for water always — the low level itself indicates the warning */
    lv_color_t water_color = lv_color_make(60, 140, 255);
    water_dsc.color = water_color;

    for (int y = water_top; y <= water_bot; y += 3) {
        lv_point_t wl = { cx - rad_w + 3, y };
        lv_point_t wr = { cx + rad_w - 3, y };
        lv_draw_line(draw_ctx, &water_dsc, &wl, &wr);
    }

    /* Wavy water surface line */
    lv_draw_line_dsc_t wave_dsc;
    lv_draw_line_dsc_init(&wave_dsc);
    wave_dsc.color = water_color;
    wave_dsc.width = 2;

    /* Simple 3-segment wave at the water surface */
    int wave_y = water_top;
    int seg_w = (2 * rad_w - 6) / 3;
    for (int s = 0; s < 3; s++) {
        int sx = cx - rad_w + 3 + s * seg_w;
        lv_point_t w0 = { sx, wave_y };
        lv_point_t w1 = { sx + seg_w / 2, wave_y - 3 + (s % 2) * 6 };
        lv_draw_line(draw_ctx, &wave_dsc, &w0, &w1);
        lv_point_t w2 = { sx + seg_w, wave_y };
        lv_draw_line(draw_ctx, &wave_dsc, &w1, &w2);
    }
}

/*******************************************************************************
 * Animation timer — rotates active fans
 ******************************************************************************/

static void fan_anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bool need_redraw = false;

    if (fan_low_active && !wading_mode) {
        fan_low_angle = (fan_low_angle + FAN_ROTATE_STEP_DEG * 10) % 3600;
        need_redraw = true;
    }
    if (fan_high_active && !wading_mode) {
        fan_high_angle = (fan_high_angle + FAN_ROTATE_STEP_DEG * 10) % 3600;
        need_redraw = true;
    }

    if (need_redraw) {
        if (fan_low_obj)  lv_obj_invalidate(fan_low_obj);
        if (fan_high_obj) lv_obj_invalidate(fan_high_obj);
    }
}

/*******************************************************************************
 * Gauge face construction
 ******************************************************************************/

static void draw_gauge_face(void)
{
    lv_color_t accent = get_accent_color(night_mode);

    /* ── Background container (circular, matching other gauges) ─────── */
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

    /* Shadows */
    create_gauge_shadows(gauge_container, night_mode);

    /* ── Title label ─────────────────────────────────────────────────── */
    title_label = lv_label_create(gauge_container);
    lv_label_set_text(title_label, "COOLING");
    lv_obj_set_style_text_color(title_label, accent, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    /* ── Fan Low icon (top-left sector) ──────────────────────────────── */
    fan_low_obj = lv_obj_create(gauge_container);
    lv_obj_set_size(fan_low_obj, 140, 120);
    lv_obj_align(fan_low_obj, LV_ALIGN_TOP_LEFT, 22, 50);
    lv_obj_clear_flag(fan_low_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(fan_low_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fan_low_obj, 0, 0);
    lv_obj_set_style_pad_all(fan_low_obj, 0, 0);
    lv_obj_add_event_cb(fan_low_obj, fan_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)0);

    fan_low_label = lv_label_create(gauge_container);
    lv_label_set_text(fan_low_label, "LOW");
    lv_obj_set_style_text_color(fan_low_label, accent, 0);
    lv_obj_set_style_text_font(fan_low_label, &lv_font_montserrat_12, 0);
    lv_obj_align(fan_low_label, LV_ALIGN_TOP_LEFT, 68, 170);

    /* ── Fan High icon (top-right sector) ────────────────────────────── */
    fan_high_obj = lv_obj_create(gauge_container);
    lv_obj_set_size(fan_high_obj, 140, 120);
    lv_obj_align(fan_high_obj, LV_ALIGN_TOP_RIGHT, -22, 50);
    lv_obj_clear_flag(fan_high_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(fan_high_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fan_high_obj, 0, 0);
    lv_obj_set_style_pad_all(fan_high_obj, 0, 0);
    lv_obj_add_event_cb(fan_high_obj, fan_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)1);

    fan_high_label = lv_label_create(gauge_container);
    lv_label_set_text(fan_high_label, "HIGH");
    lv_obj_set_style_text_color(fan_high_label, accent, 0);
    lv_obj_set_style_text_font(fan_high_label, &lv_font_montserrat_12, 0);
    lv_obj_align(fan_high_label, LV_ALIGN_TOP_RIGHT, -60, 170);

    /* ── Coolant icon (bottom sector — full width) ───────────────────── */
    coolant_obj = lv_obj_create(gauge_container);
    lv_obj_set_size(coolant_obj, 170, 110);
    lv_obj_align(coolant_obj, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_clear_flag(coolant_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(coolant_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(coolant_obj, 0, 0);
    lv_obj_set_style_pad_all(coolant_obj, 0, 0);
    lv_obj_add_event_cb(coolant_obj, coolant_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* ── Coolant temperature label (overlaid on radiator) ─────────────── */
    coolant_temp_label = lv_label_create(gauge_container);
    lv_label_set_text(coolant_temp_label, "--\u00B0C");  /* "--°C" until first reading */
    lv_obj_set_style_text_color(coolant_temp_label, accent, 0);
    lv_obj_set_style_text_font(coolant_temp_label, &lv_font_montserrat_32, 0);
    lv_obj_align(coolant_temp_label, LV_ALIGN_BOTTOM_MID, 0, -75);

    /* ── Wading mode label (hidden initially) ────────────────────────── */
    wading_label = lv_label_create(gauge_container);
    lv_label_set_text(wading_label, "WADING");
    lv_obj_set_style_text_color(wading_label, COLOR_RED, 0);
    lv_obj_set_style_text_font(wading_label, &lv_font_montserrat_16, 0);
    lv_obj_align(wading_label, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(wading_label, LV_OBJ_FLAG_HIDDEN);
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

void cooling_init(void)
{
    ESP_LOGD(TAG, "Initializing cooling gauge");

    /* Reset state (wading_mode preserved — it persists until long-press clears it) */
    fan_low_angle  = 0;
    fan_high_angle = 0;
    fan_low_active = false;
    fan_high_active = false;
    coolant_ok     = true;

    /* Draw the gauge face */
    draw_gauge_face();

    /* If wading was already active, show the wading label */
    if (wading_mode && wading_label) {
        lv_obj_clear_flag(wading_label, LV_OBJ_FLAG_HIDDEN);
    }

    /* Start rotation animation timer */
    fan_anim_timer = lv_timer_create(fan_anim_timer_cb, FAN_ROTATE_PERIOD_MS, NULL);

    is_visible = true;
    ESP_LOGD(TAG, "Cooling gauge initialized");
}

void cooling_update(void)
{
    if (!is_visible || !gauge_container) return;
    if (!exbd_has_io()) return;

    /* Ignition off — assume fans off, coolant OK, skip all input reads */
    if (!exbd_get_input(EXBD_INPUT_IGNITION)) {
        bool changed = fan_low_active || fan_high_active || !coolant_ok;
        fan_low_active  = false;
        fan_high_active = false;
        coolant_ok      = true;
        coolant_confirmed_low = false;
        coolant_overtemp = false;
        if (changed) {
            if (fan_low_obj)  lv_obj_invalidate(fan_low_obj);
            if (fan_high_obj) lv_obj_invalidate(fan_high_obj);
            if (coolant_obj)  lv_obj_invalidate(coolant_obj);
        }
        return;
    }

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    /* Read expansion board inputs — OR with manual override so animation
       runs whether the fan is activated by the vehicle or by the user. */
    bool new_fan_low  = exbd_get_input(EXBD_INPUT_FAN_LOW)  || fan_low_override;
    bool new_fan_high = exbd_get_input(EXBD_INPUT_FAN_HIGH) || fan_high_override;

    /* ── Auto-timeout for manual fan overrides (5 minutes) ────────── */
    if (fan_low_override && (now_ms - fan_low_override_start) >= FAN_OVERRIDE_TIMEOUT_MS) {
        fan_low_override = false;
        if (exbd_has_io()) mcp23017_write_pin('A', 3, false);
        ESP_LOGW(TAG, "Fan LOW override auto-off after %lu min", FAN_OVERRIDE_TIMEOUT_MS / 60000);
        Play_Music("/sdcard", "flooff.mp3");
        new_fan_low = exbd_get_input(EXBD_INPUT_FAN_LOW);
    }
    if (fan_high_override && (now_ms - fan_high_override_start) >= FAN_OVERRIDE_TIMEOUT_MS) {
        fan_high_override = false;
        if (exbd_has_io()) mcp23017_write_pin('A', 4, false);
        ESP_LOGW(TAG, "Fan HIGH override auto-off after %lu min", FAN_OVERRIDE_TIMEOUT_MS / 60000);
        Play_Music("/sdcard", "fhioff.mp3");
        new_fan_high = exbd_get_input(EXBD_INPUT_FAN_HIGH);
    }

    /* Run coolant filter here too — cooling_alarm_active() is skipped by the
       alarm system when we're already on the cooling gauge. */
    bool raw_coolant_low = exbd_get_input(EXBD_INPUT_COOLANT_LO);
    uint32_t dt_ms = (coolant_last_update_ms > 0) ? (now_ms - coolant_last_update_ms) : 100;
    coolant_last_update_ms = now_ms;
    coolant_filter_update(raw_coolant_low, dt_ms);

    bool new_coolant_ok = !coolant_confirmed_low;

    bool changed = (new_fan_low  != fan_low_active) ||
                   (new_fan_high != fan_high_active) ||
                   (new_coolant_ok != coolant_ok);

    fan_low_active  = new_fan_low;
    fan_high_active = new_fan_high;
    coolant_ok      = new_coolant_ok;

    if (changed) {
        /* Redraw icons to reflect new state colors */
        if (fan_low_obj)  lv_obj_invalidate(fan_low_obj);
        if (fan_high_obj) lv_obj_invalidate(fan_high_obj);
        if (coolant_obj)  lv_obj_invalidate(coolant_obj);

        /* Update label colors */
        lv_color_t accent = get_accent_color(night_mode);
        if (fan_low_label)  lv_obj_set_style_text_color(fan_low_label, 
            get_fan_color(fan_low_active, wading_mode), 0);
        if (fan_high_label) lv_obj_set_style_text_color(fan_high_label,
            get_fan_color(fan_high_active, wading_mode), 0);
    }
}

void cooling_set_night_mode(bool night)
{
    if (night == night_mode) return;
    night_mode = night;
    ESP_LOGD(TAG, "Night mode: %s", night ? "ON" : "OFF");

    if (!gauge_container) return;

    lv_color_t accent = get_accent_color(night_mode);

    /* Update title and labels */
    if (title_label)     lv_obj_set_style_text_color(title_label, accent, 0);
    if (fan_low_label)   lv_obj_set_style_text_color(fan_low_label,
        get_fan_color(fan_low_active, wading_mode), 0);
    if (fan_high_label)  lv_obj_set_style_text_color(fan_high_label,
        get_fan_color(fan_high_active, wading_mode), 0);

    /* Update coolant temperature label color */
    if (coolant_temp_label) lv_obj_set_style_text_color(coolant_temp_label, accent, 0);

    /* Force redraw of custom-drawn icons */
    if (fan_low_obj)  lv_obj_invalidate(fan_low_obj);
    if (fan_high_obj) lv_obj_invalidate(fan_high_obj);
    if (coolant_obj)  lv_obj_invalidate(coolant_obj);
}

void cooling_set_visible(bool visible)
{
    is_visible = visible;
    if (gauge_container) {
        if (visible) {
            lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void cooling_cleanup(void)
{
    ESP_LOGD(TAG, "Cleaning up cooling gauge");

    /* Stop animation timer */
    if (fan_anim_timer) {
        lv_timer_del(fan_anim_timer);
        fan_anim_timer = NULL;
    }

    /* NOTE: wading mode intentionally NOT cleared here.
       Wading persists until explicitly toggled off by long-press.
       OUT1 remains active. */

    /* Delete all LVGL objects */
    if (gauge_container) {
        lv_obj_del(gauge_container);
        gauge_container = NULL;
    }

    /* NULL all object pointers */
    fan_low_obj   = NULL;
    fan_high_obj  = NULL;
    fan_low_label = NULL;
    fan_high_label = NULL;
    coolant_obj   = NULL;
    coolant_temp_label = NULL;
    wading_label  = NULL;
    title_label   = NULL;

    is_visible = false;

    /* Reset coolant filter state */
    coolant_low_accum_ms  = 0;
    coolant_ok_accum_ms   = 0;
    coolant_confirmed_low = false;
    coolant_low_mp3_played = false;
    coolant_last_update_ms = 0;

    // ESP_LOGD(TAG, "Cooling gauge cleaned up");
}

void cooling_toggle_wading(void)
{
    wading_mode = !wading_mode;
    ESP_LOGW(TAG, "Wading mode: %s", wading_mode ? "ON" : "OFF");

    /* Turn off manual fan overrides when entering wading mode */
    if (wading_mode) {
        if (fan_low_override) {
            fan_low_override = false;
            if (exbd_has_io()) mcp23017_write_pin('A', 3, false);
            ESP_LOGI(TAG, "Fan low override turned off for wading");
        }
        if (fan_high_override) {
            fan_high_override = false;
            if (exbd_has_io()) mcp23017_write_pin('A', 4, false);
            ESP_LOGI(TAG, "Fan high override turned off for wading");
        }
    }

    /* Control GPA2 (wading relay) on expansion board */
    if (exbd_has_io()) {
        mcp23017_write_pin('A', 2, wading_mode);
        ESP_LOGI(TAG, "GPA2 wading %s", wading_mode ? "activated" : "deactivated");
    }

    /* Play notification MP3 (ignore if file doesn't exist) */
    if (wading_mode) {
        Play_Music("/sdcard", "wadeOn.mp3");
    } else {
        Play_Music("/sdcard", "wadeOff.mp3");
    }

    /* Update display */
    if (wading_label) {
        if (wading_mode) {
            lv_obj_clear_flag(wading_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wading_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Update fan icon colors (both turn red in wading mode) */
    if (fan_low_obj)  lv_obj_invalidate(fan_low_obj);
    if (fan_high_obj) lv_obj_invalidate(fan_high_obj);

    /* Update label colors */
    if (fan_low_label)  lv_obj_set_style_text_color(fan_low_label,
        get_fan_color(fan_low_active, wading_mode), 0);
    if (fan_high_label) lv_obj_set_style_text_color(fan_high_label,
        get_fan_color(fan_high_active, wading_mode), 0);
}

void cooling_toggle_fan_low(void)
{
    if (wading_mode) {
        ESP_LOGW(TAG, "Cannot toggle fan low — wading mode active");
        return;
    }
    fan_low_override = !fan_low_override;
    ESP_LOGW(TAG, "Manual fan LOW: %s", fan_low_override ? "ON" : "OFF");

    if (fan_low_override) {
        fan_low_override_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    /* Control GPA3 on MCP23017 expansion board */
    if (exbd_has_io()) {
        mcp23017_write_pin('A', 3, fan_low_override);
        ESP_LOGI(TAG, "GPA3 (fan low) %s", fan_low_override ? "activated" : "deactivated");
    }

    /* Play notification MP3 */
    if (fan_low_override) {
        Play_Music("/sdcard", "fanLowOn.mp3");
    } else {
        Play_Music("/sdcard", "flooff.mp3");
    }

    /* Update fan icon display */
    if (fan_low_obj)  lv_obj_invalidate(fan_low_obj);
    if (fan_low_label) lv_obj_set_style_text_color(fan_low_label,
        get_fan_color(fan_low_active, wading_mode), 0);
}

void cooling_toggle_fan_high(void)
{
    if (wading_mode) {
        ESP_LOGW(TAG, "Cannot toggle fan high — wading mode active");
        return;
    }
    fan_high_override = !fan_high_override;
    ESP_LOGW(TAG, "Manual fan HIGH: %s", fan_high_override ? "ON" : "OFF");

    if (fan_high_override) {
        fan_high_override_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    /* Control GPA4 on MCP23017 expansion board */
    if (exbd_has_io()) {
        mcp23017_write_pin('A', 4, fan_high_override);
        ESP_LOGI(TAG, "GPA4 (fan high) %s", fan_high_override ? "activated" : "deactivated");
    }

    /* Play notification MP3 */
    if (fan_high_override) {
        Play_Music("/sdcard", "fhion.mp3");
    } else {
        Play_Music("/sdcard", "fhioff.mp3");
    }

    /* Update fan icon display */
    if (fan_high_obj)  lv_obj_invalidate(fan_high_obj);
    if (fan_high_label) lv_obj_set_style_text_color(fan_high_label,
        get_fan_color(fan_high_active, wading_mode), 0);
}

bool cooling_get_fan_low_override(void)
{
    return fan_low_override;
}

bool cooling_get_fan_high_override(void)
{
    return fan_high_override;
}

bool cooling_alarm_active(void)
{
    if (!exbd_has_io()) return false;

    /* Ignition off — no valid cooling inputs, never alarm */
    if (!exbd_get_input(EXBD_INPUT_IGNITION)) return false;

    /* Run the coolant filter even when the gauge isn't visible,
       so the alarm system can detect confirmed-low from any gauge. */
    bool raw_coolant_low = exbd_get_input(EXBD_INPUT_COOLANT_LO);
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t dt_ms = (coolant_last_update_ms > 0) ? (now_ms - coolant_last_update_ms) : 100;
    coolant_last_update_ms = now_ms;
    coolant_filter_update(raw_coolant_low, dt_ms);

    /* Auto-timeout manual fan overrides even when gauge is not visible */
    if (fan_low_override && (now_ms - fan_low_override_start) >= FAN_OVERRIDE_TIMEOUT_MS) {
        fan_low_override = false;
        mcp23017_write_pin('A', 3, false);
        Play_Music("/sdcard", "flooff.mp3");
        ESP_LOGW(TAG, "Fan LOW override auto-off (background) after %lu min",
                 FAN_OVERRIDE_TIMEOUT_MS / 60000);
    }
    if (fan_high_override && (now_ms - fan_high_override_start) >= FAN_OVERRIDE_TIMEOUT_MS) {
        fan_high_override = false;
        mcp23017_write_pin('A', 4, false);
        Play_Music("/sdcard", "fhioff.mp3");
        ESP_LOGW(TAG, "Fan HIGH override auto-off (background) after %lu min",
                 FAN_OVERRIDE_TIMEOUT_MS / 60000);
    }

    bool fl = exbd_get_input(EXBD_INPUT_FAN_LOW);
    /* FanHigh alone = aircon, not a cooling alarm.
       Only trigger when FanLow is on (real cooling) or coolant confirmed low
       or coolant temperature has reached danger threshold. */
    return (fl || coolant_confirmed_low || coolant_overtemp);
}

bool cooling_get_wading(void)
{
    return wading_mode;
}

void cooling_set_coolant_temp(float temp_c)
{
    /* ── Alarm logic runs even when gauge is not visible ─────────────── */
    if (!isnan(temp_c) && temp_c >= COOLANT_TEMP_DANGER_C) {
        coolant_overtemp = true;
        if (!overheat_mp3_played) {
            overheat_mp3_played = true;
            Play_Music("/sdcard", "overheat.mp3");
            ESP_LOGW("COOLING", "Coolant OVERTEMP %.0f°C — playing overheat.mp3", temp_c);
        }
    } else if (!isnan(temp_c) && temp_c < COOLANT_TEMP_DANGER_C) {
        coolant_overtemp = false;
        overheat_mp3_played = false;
    }

    /* ── Display update only when label exists (gauge visible) ──────── */
    if (!coolant_temp_label) return;

    /* Hide if invalid or ≤ 60°C */
    if (isnan(temp_c) || temp_c <= COOLANT_TEMP_HIDE_C) {
        lv_obj_add_flag(coolant_temp_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Format and show */
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f\u00B0C", temp_c);
    lv_label_set_text(coolant_temp_label, buf);
    lv_obj_clear_flag(coolant_temp_label, LV_OBJ_FLAG_HIDDEN);

    /* Color: accent (normal), yellow (≥110), red (≥115) */
    lv_color_t color;
    if (temp_c >= COOLANT_TEMP_DANGER_C) {
        color = lv_color_make(255, 0, 0);       /* Red */
    } else if (temp_c >= COOLANT_TEMP_WARN_C) {
        color = lv_color_make(255, 200, 0);     /* Yellow */
    } else {
        color = get_accent_color(night_mode);   /* Normal */
        coolant_overtemp = false;
        overheat_mp3_played = false;
    }
    lv_obj_set_style_text_color(coolant_temp_label, color, 0);
}
