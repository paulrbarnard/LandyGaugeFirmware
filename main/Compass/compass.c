/**
 * @file compass.c
 * @brief Compass gauge display implementation using LVGL
 *
 * Displays a compass rose with cardinal/intercardinal markers positioned
 * according to the current heading, with a fixed lubber line at the top.
 * Digital heading readout in the center.
 * Visual style matches the other gauges (day/night mode, shadows).
 *
 * The compass card is redrawn on each heading update — ticks and letters
 * are positioned mathematically based on the current heading.
 */

#include "compass.h"
#include "esp_log.h"
#include "LVGL_Driver/style.h"
#include "lis3mdl.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "COMPASS";

// Display mode
static bool night_mode = true;

// Current heading in degrees (0-360, 0=North)
static float current_heading = 0.0f;

// Compass rose radii
#define CARDINAL_R     115          // Cardinal letter radius (N, E, S, W)
#define INTERCARD_R    125          // Intercardinal letter radius (NE, SE, etc.)
#define MAJOR_TICK_OR  158          // Major tick outer radius
#define MAJOR_TICK_IR  140          // Major tick inner radius
#define MINOR_TICK_OR  158          // Minor (10°) tick outer radius
#define MINOR_TICK_IR  148          // Minor tick inner radius
#define SMALL_TICK_OR  158          // Small (5°) tick outer radius
#define SMALL_TICK_IR  153          // Small tick inner radius

// Lubber line (fixed heading indicator at 12 o'clock)
#define LUBBER_OUTER   165
#define LUBBER_INNER   140

// Warning color for North marker
#define COLOR_NORTH    lv_color_make(255, 60, 0)

// Container padding offset
#define PAD_OFF 20

// LVGL objects
static lv_obj_t *gauge_container = NULL;    // Main container (background + shadows)
static lv_obj_t *card_container = NULL;     // Rotating compass card elements
static lv_obj_t *heading_label = NULL;      // Digital heading "000°"
static lv_obj_t *bearing_label = NULL;      // Cardinal bearing text "NNE"
static lv_obj_t *compass_label = NULL;      // "COMPASS" label
static lv_obj_t *cal_arc = NULL;            // Calibration progress arc
static lv_obj_t *cal_pct_label = NULL;      // Calibration percentage text
static lv_obj_t *cal_msg_label = NULL;      // "CAL DONE" / "ROTATE 360°" message
static uint32_t cal_done_time = 0;          // Tick when calibration completed (for timeout)
static bool cal_done_showing = false;       // Currently showing "CAL DONE" message

// Track allocated tick point arrays for cleanup
#define MAX_TICKS 80
static lv_point_t *tick_point_arrays[MAX_TICKS];
static int tick_point_count = 0;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

/**
 * @brief Get cardinal/intercardinal bearing text for a heading
 */
static const char *heading_to_bearing(float heading)
{
    while (heading < 0) heading += 360.0f;
    while (heading >= 360.0f) heading -= 360.0f;

    if (heading >= 348.75f || heading < 11.25f)   return "N";
    if (heading < 33.75f)  return "NNE";
    if (heading < 56.25f)  return "NE";
    if (heading < 78.75f)  return "ENE";
    if (heading < 101.25f) return "E";
    if (heading < 123.75f) return "ESE";
    if (heading < 146.25f) return "SE";
    if (heading < 168.75f) return "SSE";
    if (heading < 191.25f) return "S";
    if (heading < 213.75f) return "SSW";
    if (heading < 236.25f) return "SW";
    if (heading < 258.75f) return "WSW";
    if (heading < 281.25f) return "W";
    if (heading < 303.75f) return "WNW";
    if (heading < 326.25f) return "NW";
    return "NNW";
}

/**
 * @brief Convert compass bearing to screen angle in radians
 *
 * Takes a compass bearing (0=N, 90=E) and uses the current heading
 * to compute where the marker appears on screen.
 * Returns radians where 0=right, -π/2=top (LVGL screen coordinates).
 */
static float bearing_to_screen_rad(float bearing_deg)
{
    float rel = bearing_deg - current_heading - 90.0f;
    return rel * (float)M_PI / 180.0f;
}

/*******************************************************************************
 * Drawing
 ******************************************************************************/

/**
 * @brief Free all allocated tick point arrays
 */
static void free_tick_points(void)
{
    for (int i = 0; i < tick_point_count; i++) {
        free(tick_point_arrays[i]);
    }
    tick_point_count = 0;
}

/**
 * @brief Draw the compass card (ticks and letters) at current heading
 */
static void draw_compass_card(void)
{
    // Clean previous card elements
    if (card_container) {
        free_tick_points();
        lv_obj_clean(card_container);
    } else {
        card_container = lv_obj_create(gauge_container);
        lv_obj_set_size(card_container, DISP_W, DISP_H);
        lv_obj_center(card_container);
        lv_obj_clear_flag(card_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(card_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(card_container, 0, 0);
        lv_obj_set_style_pad_all(card_container, 0, 0);
    }

    lv_color_t accent = get_accent_color(night_mode);
    int cx = DISP_W / 2;
    int cy = DISP_H / 2;

    // Draw tick marks every 5 degrees
    for (int deg = 0; deg < 360; deg += 5) {
        bool is_cardinal = (deg % 90 == 0);
        bool is_30deg    = (deg % 30 == 0);
        bool is_10deg    = (deg % 10 == 0) && !is_30deg;

        int outer_r, inner_r, width;
        if (is_cardinal) {
            outer_r = MAJOR_TICK_OR;
            inner_r = MAJOR_TICK_IR;
            width   = 6;
        } else if (is_30deg) {
            outer_r = MAJOR_TICK_OR;
            inner_r = MAJOR_TICK_IR;
            width   = 4;
        } else if (is_10deg) {
            outer_r = MINOR_TICK_OR;
            inner_r = MINOR_TICK_IR;
            width   = 3;
        } else {
            outer_r = SMALL_TICK_OR;
            inner_r = SMALL_TICK_IR;
            width   = 2;
        }

        float rad = bearing_to_screen_rad((float)deg);
        float cos_r = cosf(rad);
        float sin_r = sinf(rad);

        lv_color_t tick_color = (deg == 0) ? COLOR_NORTH : accent;

        lv_obj_t *tick = lv_line_create(card_container);
        lv_point_t *pts = malloc(2 * sizeof(lv_point_t));
        if (tick_point_count < MAX_TICKS) {
            tick_point_arrays[tick_point_count++] = pts;
        }

        pts[0].x = cx + (int)(cos_r * outer_r);
        pts[0].y = cy + (int)(sin_r * outer_r);
        pts[1].x = cx + (int)(cos_r * inner_r);
        pts[1].y = cy + (int)(sin_r * inner_r);

        lv_line_set_points(tick, pts, 2);
        lv_obj_set_style_line_width(tick, width, 0);
        lv_obj_set_style_line_color(tick, tick_color, 0);
        lv_obj_set_style_line_rounded(tick, false, 0);
    }

    // Cardinal letters (N, E, S, W)
    const char *cardinals[] = { "N", "E", "S", "W" };
    const int card_degs[] = { 0, 90, 180, 270 };
    for (int i = 0; i < 4; i++) {
        float rad = bearing_to_screen_rad((float)card_degs[i]);
        lv_obj_t *lbl = lv_label_create(card_container);
        lv_label_set_text(lbl, cardinals[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_color_t col = (card_degs[i] == 0) ? COLOR_NORTH : accent;
        lv_obj_set_style_text_color(lbl, col, 0);

        int lx = DISP_W / 2 + (int)(cosf(rad) * CARDINAL_R);
        int ly = DISP_H / 2 + (int)(sinf(rad) * CARDINAL_R);
        lv_obj_align(lbl, LV_ALIGN_CENTER, lx - DISP_W / 2, ly - DISP_H / 2);
    }

    // Intercardinal letters (NE, SE, SW, NW)
    const char *intercards[] = { "NE", "SE", "SW", "NW" };
    const int icard_degs[] = { 45, 135, 225, 315 };
    for (int i = 0; i < 4; i++) {
        float rad = bearing_to_screen_rad((float)icard_degs[i]);
        lv_obj_t *lbl = lv_label_create(card_container);
        lv_label_set_text(lbl, intercards[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, accent, 0);

        int lx = DISP_W / 2 + (int)(cosf(rad) * INTERCARD_R);
        int ly = DISP_H / 2 + (int)(sinf(rad) * INTERCARD_R);
        lv_obj_align(lbl, LV_ALIGN_CENTER, lx - DISP_W / 2, ly - DISP_H / 2);
    }
}

/**
 * @brief Draw the fixed elements (lubber line, heading readout, shadows)
 */
static void draw_fixed_elements(void)
{
    lv_color_t accent = get_accent_color(night_mode);
    int cx = DISP_W / 2;
    int cy = DISP_H / 2;

    // Lubber line — fixed red/orange marker at 12 o'clock
    lv_obj_t *lubber = lv_line_create(gauge_container);
    lv_point_t *lpts = malloc(2 * sizeof(lv_point_t));
    lpts[0].x = cx;
    lpts[0].y = cy - LUBBER_OUTER;
    lpts[1].x = cx;
    lpts[1].y = cy - LUBBER_INNER;
    lv_line_set_points(lubber, lpts, 2);
    lv_obj_set_style_line_width(lubber, 6, 0);
    lv_obj_set_style_line_color(lubber, COLOR_NORTH, 0);
    lv_obj_set_style_line_rounded(lubber, true, 0);

    // Digital heading readout (centered)
    heading_label = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(heading_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(heading_label, accent, 0);
    lv_obj_align(heading_label, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(heading_label, "000°");

    // Bearing label (cardinal direction, below heading)
    bearing_label = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(bearing_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bearing_label, accent, 0);
    lv_obj_align(bearing_label, LV_ALIGN_CENTER, 0, 25);
    lv_label_set_text(bearing_label, "N");

    // "COMPASS" label at bottom
    compass_label = lv_label_create(gauge_container);
    lv_obj_set_style_text_font(compass_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(compass_label, accent, 0);
    lv_obj_align(compass_label, LV_ALIGN_CENTER, 0, 55);
    if (lis3mdl_is_calibrating()) {
        lv_label_set_text(compass_label, "CALIBRATING...");
        lv_obj_set_style_text_color(compass_label, COLOR_NORTH, 0);
    } else {
        lv_label_set_text(compass_label, "COMPASS");
    }

    // Recessed shadow effect (same as other gauges)
    create_gauge_shadows(gauge_container, night_mode);
}

/**
 * @brief Update the digital heading readout only
 */
static void update_heading_readout(void)
{
    if (heading_label) {
        char buf[8];
        int deg = (int)(current_heading + 0.5f) % 360;
        snprintf(buf, sizeof(buf), "%03d°", deg);
        lv_label_set_text(heading_label, buf);
    }
    if (bearing_label) {
        lv_label_set_text(bearing_label, heading_to_bearing(current_heading));
    }

    // Update calibration progress arc if calibrating
    if (lis3mdl_is_calibrating() && cal_arc) {
        float progress = lis3mdl_get_cal_progress();
        int pct = (int)(progress * 100.0f);
        lv_arc_set_value(cal_arc, pct);

        if (cal_pct_label) {
            char pct_buf[8];
            snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
            lv_label_set_text(cal_pct_label, pct_buf);
        }

    }

    // Clear "CAL DONE" message after 3 seconds
    if (cal_done_showing) {
        uint32_t now = lv_tick_get();
        if ((now - cal_done_time) > 3000) {
            cal_done_showing = false;
            // Remove calibration UI elements
            if (cal_arc) { lv_obj_del(cal_arc); cal_arc = NULL; }
            if (cal_pct_label) { lv_obj_del(cal_pct_label); cal_pct_label = NULL; }
            if (cal_msg_label) { lv_obj_del(cal_msg_label); cal_msg_label = NULL; }
            // Restore heading/bearing labels
            if (heading_label) lv_obj_clear_flag(heading_label, LV_OBJ_FLAG_HIDDEN);
            if (bearing_label) lv_obj_clear_flag(bearing_label, LV_OBJ_FLAG_HIDDEN);
            // Restore compass label
            if (compass_label) {
                lv_label_set_text(compass_label, "COMPASS");
                lv_obj_set_style_text_color(compass_label, get_accent_color(night_mode), 0);
            }
        }
    }
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

void compass_init(void)
{
    ESP_LOGD(TAG, "Initializing compass gauge");

    current_heading = 0.0f;
    tick_point_count = 0;

    // Create background container
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

    // Draw the rotating compass card first (behind fixed elements)
    draw_compass_card();

    // Draw fixed elements on top (lubber line, readout, shadows)
    draw_fixed_elements();

    update_heading_readout();

    ESP_LOGD(TAG, "Compass gauge initialized");
}

void compass_set_heading(float heading)
{
    // Normalize to 0-360
    while (heading < 0) heading += 360.0f;
    while (heading >= 360.0f) heading -= 360.0f;

    // Only redraw if heading changed by at least 1 degree (avoid flicker)
    float diff = fabsf(heading - current_heading);
    bool changed = (diff >= 1.0f && diff <= 359.0f);

    if (changed) {
        current_heading = heading;
        draw_compass_card();
    }

    // Always update readout during calibration so arc/% refreshes;
    // otherwise only when heading actually moved.
    if (changed || lis3mdl_is_calibrating()) {
        update_heading_readout();
    }
}

float compass_get_heading(void)
{
    return current_heading;
}

void compass_set_night_mode(bool is_night_mode)
{
    if (night_mode == is_night_mode) return;

    night_mode = is_night_mode;
    ESP_LOGD(TAG, "Setting %s mode", night_mode ? "night" : "day");

    // Rebuild everything with new colors
    if (gauge_container) {
        free_tick_points();
        lv_obj_clean(gauge_container);
        card_container = NULL;
        heading_label = NULL;
        bearing_label = NULL;
        compass_label = NULL;
    }
    draw_compass_card();
    draw_fixed_elements();
    update_heading_readout();
}

void compass_set_visible(bool visible)
{
    if (gauge_container) {
        if (visible) {
            lv_obj_clear_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(gauge_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void compass_cleanup(void)
{
    ESP_LOGD(TAG, "Cleaning up compass gauge");

    // If calibrating, stop it (don't apply incomplete offsets)
    if (lis3mdl_is_calibrating()) {
        lis3mdl_stop_calibration();
    }

    cal_done_showing = false;
    free_tick_points();

    if (gauge_container) {
        lv_obj_del(gauge_container);
        gauge_container = NULL;
        card_container = NULL;
        heading_label = NULL;
        bearing_label = NULL;
        compass_label = NULL;
        cal_arc = NULL;
        cal_pct_label = NULL;
        cal_msg_label = NULL;
    }

    // ESP_LOGD(TAG, "Compass gauge cleanup complete");
}

void compass_toggle_calibration(void)
{
    if (lis3mdl_is_calibrating()) {
        // Stop calibration — offsets are computed and applied automatically
        lis3mdl_stop_calibration();
        ESP_LOGW(TAG, "Calibration stopped — offsets applied");

        // Show "CAL DONE" or "CAL FAILED" message
        cal_done_showing = true;
        cal_done_time = lv_tick_get();

        bool good = lis3mdl_cal_is_good();

        // Persist calibration to NVS on success
        if (good) {
            settings_save_compass_cal();
        }

        // Update the arc to 100% and change color
        if (cal_arc) {
            lv_arc_set_value(cal_arc, good ? 100 : 0);
            lv_obj_set_style_arc_color(cal_arc, good ? lv_color_make(0, 255, 0) : COLOR_NORTH, LV_PART_INDICATOR);
        }

        // Show result in percentage label area
        if (cal_pct_label) {
            lv_label_set_text(cal_pct_label, good ? "OK" : "FAIL");
            lv_obj_set_style_text_color(cal_pct_label, good ? lv_color_make(0, 255, 0) : COLOR_NORTH, 0);
        }

        // Show message
        if (cal_msg_label) {
            lv_label_set_text(cal_msg_label, good ? "CAL DONE" : "NOT ENOUGH DATA");
            lv_obj_set_style_text_color(cal_msg_label, good ? lv_color_make(0, 255, 0) : COLOR_NORTH, 0);
        }

        // Update compass label
        if (compass_label) {
            lv_label_set_text(compass_label, good ? "CALIBRATED" : "RETRY CAL");
            lv_obj_set_style_text_color(compass_label, good ? lv_color_make(0, 255, 0) : COLOR_NORTH, 0);
        }
    } else {
        // Start calibration
        lis3mdl_start_calibration();
        ESP_LOGW(TAG, "Calibration started — rotate device");
        cal_done_showing = false;

        // Create progress arc on top of gauge
        if (!cal_arc && gauge_container) {
            cal_arc = lv_arc_create(gauge_container);
            lv_obj_set_size(cal_arc, 280, 280);
            lv_obj_center(cal_arc);
            lv_arc_set_rotation(cal_arc, 270);          // Start from top (12 o'clock)
            lv_arc_set_bg_angles(cal_arc, 0, 360);      // Full circle background
            lv_arc_set_range(cal_arc, 0, 100);
            lv_arc_set_value(cal_arc, 0);
            lv_arc_set_mode(cal_arc, LV_ARC_MODE_NORMAL);
            lv_obj_clear_flag(cal_arc, LV_OBJ_FLAG_CLICKABLE);

            // Style the background arc (dim)
            lv_obj_set_style_arc_width(cal_arc, 8, LV_PART_MAIN);
            lv_obj_set_style_arc_color(cal_arc, lv_color_make(40, 40, 40), LV_PART_MAIN);
            lv_obj_set_style_arc_opa(cal_arc, LV_OPA_COVER, LV_PART_MAIN);

            // Style the indicator arc (bright orange, fills as progress)
            lv_obj_set_style_arc_width(cal_arc, 8, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(cal_arc, COLOR_NORTH, LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(cal_arc, true, LV_PART_INDICATOR);

            // Hide the knob
            lv_obj_set_style_bg_opa(cal_arc, LV_OPA_TRANSP, LV_PART_KNOB);
            lv_obj_set_style_pad_all(cal_arc, 0, LV_PART_KNOB);
        }

        // Create percentage label in center (replaces heading temporarily)
        if (!cal_pct_label && gauge_container) {
            cal_pct_label = lv_label_create(gauge_container);
            lv_obj_set_style_text_font(cal_pct_label, &lv_font_montserrat_32, 0);
            lv_obj_set_style_text_color(cal_pct_label, COLOR_NORTH, 0);
            lv_obj_align(cal_pct_label, LV_ALIGN_CENTER, 0, -10);
            lv_label_set_text(cal_pct_label, "0%");
        }

        // Instruction message
        if (!cal_msg_label && gauge_container) {
            cal_msg_label = lv_label_create(gauge_container);
            lv_obj_set_style_text_font(cal_msg_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(cal_msg_label, COLOR_NORTH, 0);
            lv_obj_align(cal_msg_label, LV_ALIGN_CENTER, 0, 25);
            lv_label_set_text(cal_msg_label, "ROTATE 360");
        }

        // Hide the normal heading/bearing labels during calibration
        if (heading_label) lv_obj_add_flag(heading_label, LV_OBJ_FLAG_HIDDEN);
        if (bearing_label) lv_obj_add_flag(bearing_label, LV_OBJ_FLAG_HIDDEN);

        // Update compass label
        if (compass_label) {
            lv_label_set_text(compass_label, "CALIBRATING...");
            lv_obj_set_style_text_color(compass_label, COLOR_NORTH, 0);
        }
    }
}

bool compass_is_calibrating(void)
{
    return lis3mdl_is_calibrating();
}
