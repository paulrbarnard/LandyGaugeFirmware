/**
 * @file incline.c
 * @brief Incline (pitch) gauge display implementation using LVGL
 *        Displays a static side-view vehicle image with a rotating vertical
 *        plumb line that stays aligned with gravity regardless of pitch.
 *        Top and bottom arc scales show the inclination angle.
 *        Three display modes: degrees, 1-in-X gradient, % slope.
 */

#include "incline.h"
#include "lvgl.h"
#include "LVGL_Driver/style.h"
#include "Settings/settings.h"
#include "IMU/imu_attitude.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "INCLINE";

// Display parameters
#define INCLINE_SIZE    DISP_W          // 360
#define INCLINE_CX      (DISP_W / 2)
#define INCLINE_CY      (DISP_H / 2)

// Side-view image is 292x117
#define SIDE_IMG_WIDTH  292
#define SIDE_IMG_HEIGHT 117

// Scale parameters — same inset as tilt gauge
#define SCALE_RADIUS    (INCLINE_SIZE / 2 - 38)
#define SCALE_MAX_ANGLE 45

// Redraw threshold (degrees) to avoid constant invalidation
#define INCLINE_REDRAW_THRESHOLD  1.0f

// Low-pass filter coefficient (0..1): smaller = more damping
#define INCLINE_SMOOTHING  0.15f

// Display value modes
typedef enum {
    INCLINE_MODE_DEGREES = 0,   // "15.2°"
    INCLINE_MODE_ONE_IN_X,      // "1 in 3.7"
    INCLINE_MODE_PERCENT,       // "26.8%"
    INCLINE_MODE_COUNT
} incline_display_mode_t;

static incline_display_mode_t display_mode = INCLINE_MODE_DEGREES;

// Runtime state
static bool  night_mode = false;
static float current_incline_angle = 0.0f;
static float incline_offset = 0.0f;

// Scale line color (follows accent color)
static lv_color_t scale_line_color;

// External images
#include "sd_images.h"

// LVGL objects
static lv_obj_t *incline_gauge    = NULL;
static lv_obj_t *incline_img      = NULL;
static lv_obj_t *top_scale_obj    = NULL;
static lv_obj_t *bottom_scale_obj = NULL;

// Arc centre coordinates (computed once at init from layout)
static lv_point_t   top_arc_center;
static lv_point_t   bottom_arc_center;

/* ── Mode-specific tick definitions ─────────────────────────────────── */
#define MAX_TICKS 5
typedef struct {
    float angle_deg;    // offset from arc centre (always positive)
    char  label[8];     // display text
    int   tick_len;     // tick line length in pixels
} tick_def_t;

static int get_current_ticks(tick_def_t *out)
{
    switch (display_mode) {
    case INCLINE_MODE_DEGREES:
        out[0] = (tick_def_t){15.0f, "15",  8};
        out[1] = (tick_def_t){30.0f, "30", 12};
        out[2] = (tick_def_t){45.0f, "45",  8};
        return 3;
    case INCLINE_MODE_PERCENT:
        out[0] = (tick_def_t){atanf(0.25f) * 180.0f / (float)M_PI, "25%",  8};
        out[1] = (tick_def_t){atanf(0.50f) * 180.0f / (float)M_PI, "50%", 12};
        out[2] = (tick_def_t){atanf(0.75f) * 180.0f / (float)M_PI, "75%",  8};
        out[3] = (tick_def_t){45.0f,                                "100%", 8};
        return 4;
    case INCLINE_MODE_ONE_IN_X:
        out[0] = (tick_def_t){atanf(1.0f / 3.0f) * 180.0f / (float)M_PI, "1:3",  8};
        out[1] = (tick_def_t){atanf(0.5f)        * 180.0f / (float)M_PI, "1:2", 12};
        out[2] = (tick_def_t){45.0f,                                      "1:1",  8};
        return 3;
    default:
        return 0;
    }
}

/* ── Helper: draw ticks and labels for one side of an arc ───────────── */
static void draw_tick_pair(lv_draw_ctx_t *draw_ctx,
                           lv_draw_line_dsc_t *tick_dsc,
                           lv_draw_label_dsc_t *label_dsc,
                           int32_t cx, int32_t cy, int32_t radius,
                           float center_deg, const tick_def_t *td)
{
    int tl = td->tick_len;

    /* Primary tick (counter-clockwise from centre) */
    float pri_deg = center_deg - td->angle_deg;
    float pri_rad = pri_deg * (float)M_PI / 180.0f;
    float cp = cosf(pri_rad), sp = sinf(pri_rad);

    lv_point_t p0 = { cx + (int32_t)(radius * cp),        cy + (int32_t)(radius * sp) };
    lv_point_t p1 = { cx + (int32_t)((radius + tl) * cp), cy + (int32_t)((radius + tl) * sp) };
    lv_draw_line(draw_ctx, tick_dsc, &p0, &p1);

    /* Mirror tick (clockwise from centre) */
    float mir_deg = center_deg + td->angle_deg;
    float mir_rad = mir_deg * (float)M_PI / 180.0f;
    float cm = cosf(mir_rad), sm = sinf(mir_rad);

    lv_point_t m0 = { cx + (int32_t)(radius * cm),        cy + (int32_t)(radius * sm) };
    lv_point_t m1 = { cx + (int32_t)((radius + tl) * cm), cy + (int32_t)((radius + tl) * sm) };
    lv_draw_line(draw_ctx, tick_dsc, &m0, &m1);

    /* Labels outside the ticks — centered on the radial line */
    #define LBL_HALF_W 20
    #define LBL_HALF_H  8
    #define LBL_OUTSET 22  /* distance from arc to label centre */

    lv_area_t pri_lbl = {
        .x1 = cx + (int32_t)((radius + LBL_OUTSET) * cp) - LBL_HALF_W,
        .y1 = cy + (int32_t)((radius + LBL_OUTSET) * sp) - LBL_HALF_H,
    };
    pri_lbl.x2 = pri_lbl.x1 + LBL_HALF_W * 2;
    pri_lbl.y2 = pri_lbl.y1 + LBL_HALF_H * 2;
    lv_draw_label(draw_ctx, label_dsc, &pri_lbl, td->label, NULL);

    lv_area_t mir_lbl = {
        .x1 = cx + (int32_t)((radius + LBL_OUTSET) * cm) - LBL_HALF_W,
        .y1 = cy + (int32_t)((radius + LBL_OUTSET) * sm) - LBL_HALF_H,
    };
    mir_lbl.x2 = mir_lbl.x1 + LBL_HALF_W * 2;
    mir_lbl.y2 = mir_lbl.y1 + LBL_HALF_H * 2;
    lv_draw_label(draw_ctx, label_dsc, &mir_lbl, td->label, NULL);

    #undef LBL_HALF_W
    #undef LBL_HALF_H
    #undef LBL_OUTSET
}

/* ── Helper: draw the centre (0°) reference tick ────────────────────── */
static void draw_center_tick(lv_draw_ctx_t *draw_ctx,
                             lv_draw_line_dsc_t *tick_dsc,
                             int32_t cx, int32_t cy, int32_t radius,
                             float center_deg)
{
    float rad = center_deg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    int tl = 15;  /* longer tick for centre mark */
    lv_point_t a = { cx + (int32_t)(radius * c),        cy + (int32_t)(radius * s) };
    lv_point_t b = { cx + (int32_t)((radius + tl) * c), cy + (int32_t)((radius + tl) * s) };
    lv_draw_line(draw_ctx, tick_dsc, &a, &b);
}

/* ── Helper: draw a small triangular pointer on the inside of an arc ─ */
static void draw_pointer(lv_draw_ctx_t *draw_ctx, int32_t cx, int32_t cy,
                         int32_t radius, float angle_deg, bool top_side)
{
    /* Clamp the displayed angle to the scale range */
    float clamped = current_incline_angle;
    if (clamped >  SCALE_MAX_ANGLE) clamped =  (float)SCALE_MAX_ANGLE;
    if (clamped < -SCALE_MAX_ANGLE) clamped = -(float)SCALE_MAX_ANGLE;

    /* For the top arc, centre is 270°; for bottom, 90°.
       Positive pitch (nose up) → both pointers move in the same angular
       direction relative to their arc centre. */
    float center_deg = top_side ? 270.0f : 90.0f;
    float ptr_deg = center_deg - clamped;
    float ptr_rad = ptr_deg * (float)M_PI / 180.0f;

    /* Pointer tip sits on the arc, base is inward */
    #define PTR_LEN    10
    #define PTR_HALF_W  5
    float cp = cosf(ptr_rad), sp = sinf(ptr_rad);

    /* Tip (on the arc) */
    lv_point_t tip = {
        cx + (int32_t)(radius * cp),
        cy + (int32_t)(radius * sp)
    };

    /* Base centre (inward from arc) */
    int32_t base_r = radius - PTR_LEN;
    float bx = (float)cx + (float)base_r * cp;
    float by = (float)cy + (float)base_r * sp;

    /* Perpendicular offsets for triangle base corners */
    float perp_x = -sp * (float)PTR_HALF_W;
    float perp_y =  cp * (float)PTR_HALF_W;

    lv_point_t tri[3] = {
        tip,
        { (int32_t)(bx + perp_x), (int32_t)(by + perp_y) },
        { (int32_t)(bx - perp_x), (int32_t)(by - perp_y) },
    };

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = scale_line_color;
    rect_dsc.bg_opa   = LV_OPA_COVER;

    /* Draw as three lines forming a filled triangle */
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = scale_line_color;
    dsc.width = 2;
    dsc.opa   = LV_OPA_COVER;
    lv_draw_line(draw_ctx, &dsc, &tri[0], &tri[1]);
    lv_draw_line(draw_ctx, &dsc, &tri[1], &tri[2]);
    lv_draw_line(draw_ctx, &dsc, &tri[2], &tri[0]);

    /* Fill interior by drawing lines from tip to each base point */
    for (int i = 0; i <= PTR_HALF_W * 2; i++) {
        float t = (float)i / (float)(PTR_HALF_W * 2);
        lv_point_t bp = {
            (int32_t)(bx - perp_x + 2.0f * perp_x * t),
            (int32_t)(by - perp_y + 2.0f * perp_y * t)
        };
        lv_draw_line(draw_ctx, &dsc, &tip, &bp);
    }
    #undef PTR_LEN
    #undef PTR_HALF_W
}

/* ── Draw callback: top arc scale ───────────────────────────────────── */
static void draw_top_scale_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    int32_t cx = top_arc_center.x, cy = top_arc_center.y;

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = scale_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa   = LV_OPA_COVER;
    lv_draw_arc(draw_ctx, &arc_dsc, &top_arc_center, SCALE_RADIUS, 225, 315);

    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = scale_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa   = LV_OPA_COVER;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color      = scale_line_color;
    label_dsc.opa        = LV_OPA_COVER;
    label_dsc.align      = LV_TEXT_ALIGN_CENTER;

    /* Centre reference tick */
    draw_center_tick(draw_ctx, &tick_dsc, cx, cy, SCALE_RADIUS, 270.0f);

    /* Mode-specific graduated ticks */
    tick_def_t ticks[MAX_TICKS];
    int count = get_current_ticks(ticks);
    for (int i = 0; i < count; i++) {
        draw_tick_pair(draw_ctx, &tick_dsc, &label_dsc,
                       cx, cy, SCALE_RADIUS, 270.0f, &ticks[i]);
    }

    /* Moving pointer on inside of arc */
    draw_pointer(draw_ctx, cx, cy, SCALE_RADIUS, current_incline_angle, true);
}

/* ── Draw callback: bottom arc scale ────────────────────────────────── */
static void draw_bottom_scale_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    int32_t cx = bottom_arc_center.x, cy = bottom_arc_center.y;

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = scale_line_color;
    arc_dsc.width = 3;
    arc_dsc.opa   = LV_OPA_COVER;
    lv_draw_arc(draw_ctx, &arc_dsc, &bottom_arc_center, SCALE_RADIUS, 45, 135);

    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = scale_line_color;
    tick_dsc.width = 2;
    tick_dsc.opa   = LV_OPA_COVER;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color      = scale_line_color;
    label_dsc.opa        = LV_OPA_COVER;
    label_dsc.align      = LV_TEXT_ALIGN_CENTER;

    /* Centre reference tick */
    draw_center_tick(draw_ctx, &tick_dsc, cx, cy, SCALE_RADIUS, 90.0f);

    /* Mode-specific graduated ticks */
    tick_def_t ticks[MAX_TICKS];
    int count = get_current_ticks(ticks);
    for (int i = 0; i < count; i++) {
        draw_tick_pair(draw_ctx, &tick_dsc, &label_dsc,
                       cx, cy, SCALE_RADIUS, 90.0f, &ticks[i]);
    }

    /* Moving pointer on inside of arc */
    draw_pointer(draw_ctx, cx, cy, SCALE_RADIUS, current_incline_angle, false);
}

/* ══════════════════════════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════════════════════════ */

void incline_init(void)
{
    ESP_LOGD(TAG, "Initializing incline gauge");

    /* Root container */
    incline_gauge = lv_obj_create(lv_scr_act());
    lv_obj_set_size(incline_gauge, 360, 360);
    lv_obj_center(incline_gauge);
    lv_obj_set_style_bg_color(incline_gauge, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_bg_opa(incline_gauge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(incline_gauge, 0, 0);
    lv_obj_set_style_radius(incline_gauge, 0, 0);
    lv_obj_clear_flag(incline_gauge, LV_OBJ_FLAG_SCROLLABLE);

    scale_line_color = get_accent_color(night_mode);

    /* Side-view vehicle image */
    incline_img = lv_img_create(incline_gauge);
    lv_img_set_src(incline_img, sd_images_get_side(night_mode));
    lv_obj_align(incline_img, LV_ALIGN_CENTER, 0, 0);
    /* Pivot at image centre; angle applied in incline_set_angle() */
    lv_img_set_pivot(incline_img, SIDE_IMG_WIDTH / 2, SIDE_IMG_HEIGHT / 2);
    lv_img_set_antialias(incline_img, true);

    /* Top arc scale */
    top_scale_obj = lv_obj_create(incline_gauge);
    lv_obj_set_size(top_scale_obj, INCLINE_SIZE, INCLINE_SIZE);
    lv_obj_center(top_scale_obj);
    lv_obj_set_style_bg_opa(top_scale_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_scale_obj, 0, 0);
    lv_obj_clear_flag(top_scale_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(top_scale_obj, draw_top_scale_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* Bottom arc scale */
    bottom_scale_obj = lv_obj_create(incline_gauge);
    lv_obj_set_size(bottom_scale_obj, INCLINE_SIZE, INCLINE_SIZE);
    lv_obj_center(bottom_scale_obj);
    lv_obj_set_style_bg_opa(bottom_scale_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_scale_obj, 0, 0);
    lv_obj_clear_flag(bottom_scale_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bottom_scale_obj, draw_bottom_scale_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* Compute arc centre coordinates from layout */
    lv_obj_update_layout(incline_gauge);
    {
        lv_area_t tc;
        lv_obj_get_coords(top_scale_obj, &tc);
        top_arc_center.x = (tc.x1 + tc.x2) / 2;
        top_arc_center.y = (tc.y1 + tc.y2) / 2;

        lv_area_t bc;
        lv_obj_get_coords(bottom_scale_obj, &bc);
        bottom_arc_center.x = (bc.x1 + bc.x2) / 2;
        bottom_arc_center.y = (bc.y1 + bc.y2) / 2;
    }

    /* Shadow overlays */
    create_gauge_shadows(incline_gauge, night_mode);

    ESP_LOGD(TAG, "Incline gauge initialised");
}

void incline_set_visible(bool visible)
{
    if (!incline_gauge) return;
    if (visible) {
        lv_obj_clear_flag(incline_gauge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(incline_gauge, LV_OBJ_FLAG_HIDDEN);
    }
}

void incline_zero_offset(void)
{
    incline_offset = 0.0f;
    incline_offset = imu_get_pitch();
    ESP_LOGI(TAG, "Incline zeroed: offset = %.1f°", incline_offset);
    settings_save_incline_offset(incline_offset);
    current_incline_angle = 999.0f;   // force redraw
    incline_set_angle(imu_get_pitch());
}

void incline_set_offset(float offset_deg)
{
    incline_offset = offset_deg;
    ESP_LOGI(TAG, "Incline offset restored: %.1f°", incline_offset);
}

void incline_set_angle(float angle_degrees)
{
    float corrected = angle_degrees - incline_offset;

    /* Low-pass filter for smoother needle movement */
    corrected = current_incline_angle + INCLINE_SMOOTHING * (corrected - current_incline_angle);

    if (fabsf(corrected - current_incline_angle) < INCLINE_REDRAW_THRESHOLD)
        return;

    current_incline_angle = corrected;

    /* Rotate the vehicle image — LVGL angle is in tenths of a degree.
       Positive pitch (nose up) should tilt image nose-up, i.e. clockwise
       rotation in screen coords → negative LVGL angle. */
    if (incline_img) {
        lv_img_set_angle(incline_img, (int16_t)(-current_incline_angle * 10.0f));
    }

    /* Invalidate scale arcs so pointers and labels redraw */
    if (top_scale_obj)    lv_obj_invalidate(top_scale_obj);
    if (bottom_scale_obj) lv_obj_invalidate(bottom_scale_obj);
}

void incline_set_night_mode(bool night)
{
    if (night_mode == night) return;
    night_mode = night;
    scale_line_color = get_accent_color(night_mode);

    if (incline_img) {
        lv_img_set_src(incline_img, sd_images_get_side(night_mode));
    }
    if (top_scale_obj)  lv_obj_invalidate(top_scale_obj);
    if (bottom_scale_obj) lv_obj_invalidate(bottom_scale_obj);
    /* Recreate shadows: keep image(0) top(1) bottom(2) */
    if (incline_gauge) {
        uint32_t child_count = lv_obj_get_child_cnt(incline_gauge);
        while (child_count > 3) {
            lv_obj_del(lv_obj_get_child(incline_gauge, child_count - 1));
            child_count--;
        }
        create_gauge_shadows(incline_gauge, night_mode);
    }
}

void incline_cycle_mode(void)
{
    display_mode = (incline_display_mode_t)((display_mode + 1) % INCLINE_MODE_COUNT);
    settings_save_incline_mode((uint8_t)display_mode);
    /* Invalidate scales so tick labels redraw in new units */
    if (top_scale_obj)    lv_obj_invalidate(top_scale_obj);
    if (bottom_scale_obj) lv_obj_invalidate(bottom_scale_obj);
    ESP_LOGI(TAG, "Display mode → %d", (int)display_mode);
}

void incline_set_mode(uint8_t mode)
{
    if (mode < INCLINE_MODE_COUNT) {
        display_mode = (incline_display_mode_t)mode;
    }
}

void incline_cleanup(void)
{
    ESP_LOGD(TAG, "Cleaning up incline gauge");

    if (incline_gauge) {
        lv_obj_del(incline_gauge);
        incline_gauge    = NULL;
        incline_img      = NULL;
        top_scale_obj    = NULL;
        bottom_scale_obj = NULL;
    }
}
