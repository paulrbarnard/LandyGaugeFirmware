/**
 * @file clock.c
 * @brief Analog clock display implementation using LVGL
 */

#include "clock.h"
#include "PCF85063.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "LVGL_Driver/style.h"

static const char *TAG = "CLOCK";

// Display mode - can be changed at runtime
static bool night_mode = true; // Night mode (green hands)


// Clock dimensions
#define CLOCK_SIZE 360                  // Clock diameter
#define TICK_HOFF 20                    // Horizontal offset for tick centering
#define TICK_YOFF 20                    // Vertical offset for tick centering
#define CLOCK_CENTER_X (CLOCK_SIZE / 2) // Container center X
#define CLOCK_CENTER_Y (CLOCK_SIZE / 2) // Container center Y
#define HOUR_HAND_LENGTH 84             // 70% of minute hand length, -5 for clearance
#define MINUTE_HAND_LENGTH 122          // Reaches inside of hour tick marks, -5 for clearance
#define CENTER_DOT_SIZE 64              // 25% smaller from 85

// LVGL objects
static lv_obj_t *clock_face = NULL;
static lv_obj_t *hour_hand = NULL;
static lv_obj_t *minute_hand = NULL;
static lv_obj_t *hour_shadow = NULL;
static lv_obj_t *minute_shadow = NULL;

// Update timer
static lv_timer_t *clock_timer = NULL;

// Hand points (line coordinates)
static lv_point_t hour_points[2];
static lv_point_t minute_points[2];
static lv_point_t hour_shadow_points[2];
static lv_point_t minute_shadow_points[2];

// Shadow offset in pixels (down and to the right)
#define SHADOW_OFS_X 3
#define SHADOW_OFS_Y 3
#define SHADOW_COLOR lv_color_make(40, 40, 40)
#define SHADOW_OPA LV_OPA_60


static inline int16_t clamp_x(int16_t x)
{
    if (x < 0)
        return 0;
    if (x > DISP_W - 1)
        return DISP_W - 1;
    return x;
}

static inline int16_t clamp_y(int16_t y)
{
    if (y < 0)
        return 0;
    if (y > DISP_H - 1)
        return DISP_H - 1;
    return y;
}

/**
 * @brief Draw clock face with hour markers and numbers
 */
static void draw_clock_face(void)
{
    // if (clock_face) lv_obj_del(clock_face);
    if (!clock_face)
    {
        clock_face = lv_obj_create(lv_scr_act());

        // After creating clock_face:
        lv_obj_set_size(clock_face, DISP_W, DISP_H);
        lv_obj_center(clock_face);

        // Don’t let layout/children expand it
        lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_OVERFLOW_VISIBLE); // clip to 360×360

        lv_obj_set_style_bg_color(clock_face, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(clock_face, LV_OPA_100, 0);
        lv_obj_set_style_border_width(clock_face, 1, 0);
        lv_obj_set_style_border_color(clock_face, COLOR_BACKGROUND, 0); //
        // lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, 0);
        lv_obj_move_background(clock_face);
        ESP_LOGD(TAG, "clock_face created and centered");
    }
    else
    {
        // Clean existing children before redrawing (prevents shadow/element accumulation)
        lv_obj_clean(clock_face);
        ESP_LOGD(TAG, "clock_face children cleaned for redraw");
    }

    // Get true center from container size
    int center_x = CLOCK_CENTER_X;
    int center_y = CLOCK_CENTER_Y;


    // Draw hour markers and numbers
    for (int i = 0; i < 12; i++)
    {
        float angle = (i * 30 - 90) * M_PI / 180.0;
        int marker_start, marker_end;
        int line_width;
        // Use fixed radii from the center for tick marks
        if (i == 0 || i == 3 || i == 6 || i == 9)
        {
            marker_start = (CLOCK_SIZE / 2) - 42;
            marker_end = (CLOCK_SIZE / 2) - 19;
            line_width = 8;
        }
        else
        {
            marker_start = (CLOCK_SIZE / 2) - 42;
            marker_end = (CLOCK_SIZE / 2) - 19;
            line_width = 4;
        }
        lv_obj_t *marker = lv_line_create(clock_face);
        lv_point_t *marker_points = malloc(2 * sizeof(lv_point_t));

        int x = center_x + cosf(angle) * marker_start - TICK_HOFF;
        int y = center_y + sinf(angle) * marker_start - TICK_YOFF;
        marker_points[0].x = clamp_x(x);
        marker_points[0].y = clamp_y(y);

        x = center_x + cosf(angle) * marker_end - TICK_HOFF;
        y = center_y + sinf(angle) * marker_end - TICK_YOFF;
        marker_points[1].x = clamp_x(x);
        marker_points[1].y = clamp_y(y);

        lv_line_set_points(marker, marker_points, 2);
        lv_obj_set_style_line_width(marker, line_width, 0);
        lv_obj_set_style_line_color(marker, get_accent_color(night_mode), 0);
        lv_obj_set_style_line_rounded(marker, false, 0);
        // ESP_LOGD(TAG, "Tick mark %d created at (%d,%d)->(%d,%d)", i, marker_points[0].x, marker_points[0].y, marker_points[1].x, marker_points[1].y);
        if (i == 0 || i == 3 || i == 6 || i == 9)
        {
            int number_radius = (CLOCK_SIZE / 2) - 60;
            lv_obj_t *num_label = lv_label_create(clock_face);
            const char *num_text;
            switch (i)
            {
            case 0:
                num_text = "12";
                break;
            case 3:
                num_text = "3";
                break;
            case 6:
                num_text = "6";
                break;
            case 9:
                num_text = "9";
                break;
            default:
                num_text = "";
                break;
            }
            lv_label_set_text(num_label, num_text);
            lv_obj_set_style_text_color(num_label, get_accent_color(night_mode), 0);
            lv_obj_set_style_text_font(num_label, &lv_font_montserrat_32, 0);
            int num_x = center_x + cos(angle) * number_radius;
            int num_y = center_y + sin(angle) * number_radius;
            lv_obj_align(num_label, LV_ALIGN_CENTER,
                         num_x - center_x - 2,
                         num_y - center_y - 4);
            // ESP_LOGD(TAG, "Hour number %s created at (%d,%d)", num_text, num_x, num_y);
              // SAFE TO FREE — LVGL makes its own copy
            //free(marker_points);

        }
    }

    // add the logo
    LV_IMG_DECLARE(logo_img); // from the generated .c file

     lv_obj_t *logo = lv_img_create(clock_face);
    lv_img_set_src(logo, &logo_img);
   lv_obj_align(logo, LV_ALIGN_CENTER, 0, 70); // Set y offset below center cap

   // enable recolor
lv_obj_set_style_img_recolor_opa(logo, LV_OPA_COVER, LV_PART_MAIN);
// set the recolor color (green)
lv_obj_set_style_img_recolor(logo, get_accent_color(night_mode), LV_PART_MAIN);

    // Create recessed shadow effects
    create_gauge_shadows(clock_face, night_mode);

  }

/**
 * @brief Create clock hands (lines)
 */
static void create_hands(void)
{
    // Layer order (bottom to top): hour shadow, hour hand, minute shadow, minute hand

    // Hour shadow (bottommost)
    hour_shadow = lv_line_create(clock_face);
    lv_obj_set_style_line_width(hour_shadow, 12, 0);
    lv_obj_set_style_line_color(hour_shadow, SHADOW_COLOR, 0);
    lv_obj_set_style_line_opa(hour_shadow, SHADOW_OPA, 0);
    lv_obj_set_style_line_rounded(hour_shadow, true, 0);

    // Hour hand
    hour_hand = lv_line_create(clock_face);
    lv_obj_set_style_line_width(hour_hand, 12, 0);
    lv_obj_set_style_line_color(hour_hand, get_accent_color(night_mode), 0);
    lv_obj_set_style_line_rounded(hour_hand, true, 0);

    // Minute shadow (on top of hour hand)
    minute_shadow = lv_line_create(clock_face);
    lv_obj_set_style_line_width(minute_shadow, 9, 0);
    lv_obj_set_style_line_color(minute_shadow, SHADOW_COLOR, 0);
    lv_obj_set_style_line_opa(minute_shadow, SHADOW_OPA, 0);
    lv_obj_set_style_line_rounded(minute_shadow, true, 0);

    // Minute hand (topmost, before center cap)
    minute_hand = lv_line_create(clock_face);
    lv_obj_set_style_line_width(minute_hand, 9, 0);
    lv_obj_set_style_line_color(minute_hand, get_accent_color(night_mode), 0);
    lv_obj_set_style_line_rounded(minute_hand, true, 0);

    // Create center cap (raised button appearance) - created last to be on top
    lv_obj_t *center = lv_obj_create(clock_face);
    lv_obj_set_size(center, CENTER_DOT_SIZE, CENTER_DOT_SIZE);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center, 0, 0);

    // Create gradient effect - dark grey to black
    lv_obj_set_style_bg_color(center, lv_color_make(60, 60, 60), 0);   // Lighter (top-right)
    lv_obj_set_style_bg_grad_color(center, lv_color_make(0, 0, 0), 0); // Darker black (bottom-left)
    lv_obj_set_style_bg_grad_dir(center, LV_GRAD_DIR_HOR, 0);          // Horizontal gradient

    // Add subtle highlight shadow from top-left for enhanced raised effect
    lv_obj_set_style_shadow_width(center, 10, 0);
    lv_obj_set_style_shadow_opa(center, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(center, get_accent_color(night_mode), 0);
    lv_obj_set_style_shadow_ofs_x(center, -3, 0);
    lv_obj_set_style_shadow_ofs_y(center, -3, 0);

    lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
}

/**
 * @brief Timer callback that updates the clock display every second from RTC
 */
static void clock_timer_cb(lv_timer_t *timer)
{
    datetime_t current_time;

    // Read current time from RTC
    PCF85063_Read_Time(&current_time);

    // Update clock display
    clock_update(current_time.hour, current_time.minute, current_time.second);
}

void clock_init(void)
{
    ESP_LOGD(TAG, "Initializing analog clock");

    // Draw the clock face
    draw_clock_face();

    // Create the hands
    create_hands();

    // Read initial time from RTC and display it
    datetime_t current_time;
    PCF85063_Read_Time(&current_time);
    clock_update(current_time.hour, current_time.minute, current_time.second);
    ESP_LOGD(TAG, "Initial time set to %02d:%02d:%02d",
             current_time.hour, current_time.minute, current_time.second);

    // Create LVGL timer to update clock every second (1000ms)
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    if (clock_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create clock timer");
        return;
    }

    ESP_LOGD(TAG, "Clock initialized with LVGL timer");
}

void clock_update(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!hour_hand || !minute_hand)
        return;
    // Get true center from container size
    int center_x = CLOCK_CENTER_X - TICK_HOFF;
    int center_y = CLOCK_CENTER_Y - TICK_YOFF;
    float hour_angle = ((hour % 12) * 30 + minute * 0.5 - 90) * M_PI / 180.0;
    float minute_angle = (minute * 6 + second * 0.1 - 90) * M_PI / 180.0;

    // Update hour hand shadow (offset down-right)
    hour_shadow_points[0].x = center_x + SHADOW_OFS_X;
    hour_shadow_points[0].y = center_y + SHADOW_OFS_Y;
    hour_shadow_points[1].x = center_x + cos(hour_angle) * HOUR_HAND_LENGTH + SHADOW_OFS_X;
    hour_shadow_points[1].y = center_y + sin(hour_angle) * HOUR_HAND_LENGTH + SHADOW_OFS_Y;
    if (hour_shadow) lv_line_set_points(hour_shadow, hour_shadow_points, 2);

    // Update minute hand shadow (offset down-right)
    minute_shadow_points[0].x = center_x + SHADOW_OFS_X;
    minute_shadow_points[0].y = center_y + SHADOW_OFS_Y;
    minute_shadow_points[1].x = center_x + cos(minute_angle) * MINUTE_HAND_LENGTH + SHADOW_OFS_X;
    minute_shadow_points[1].y = center_y + sin(minute_angle) * MINUTE_HAND_LENGTH + SHADOW_OFS_Y;
    if (minute_shadow) lv_line_set_points(minute_shadow, minute_shadow_points, 2);

    // Update hour hand
    hour_points[0].x = center_x;
    hour_points[0].y = center_y;
    hour_points[1].x = center_x + cos(hour_angle) * HOUR_HAND_LENGTH;
    hour_points[1].y = center_y + sin(hour_angle) * HOUR_HAND_LENGTH;
    lv_line_set_points(hour_hand, hour_points, 2);
    // Update minute hand
    minute_points[0].x = center_x;
    minute_points[0].y = center_y;
    minute_points[1].x = center_x + cos(minute_angle) * MINUTE_HAND_LENGTH;
    minute_points[1].y = center_y + sin(minute_angle) * MINUTE_HAND_LENGTH;
    lv_line_set_points(minute_hand, minute_points, 2);
}

void clock_set_night_mode(bool is_night_mode)
{
    if (night_mode != is_night_mode)
    {
        night_mode = is_night_mode;
        ESP_LOGD(TAG, "Clock mode changed to: %s", night_mode ? "NIGHT (green)" : "DAY (white)");
        // Redraw the entire clock with new colors
        draw_clock_face();
        create_hands();
        // Restore current time
        datetime_t current_time;
        PCF85063_Read_Time(&current_time);
        clock_update(current_time.hour, current_time.minute, current_time.second);
    }
}

void clock_set_visible(bool visible)
{
    if (clock_face)
    {
        if (visible)
        {
            lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(clock_face);  // Ensure clock is on top
            // Resume the timer when becoming visible
            if (clock_timer) {
                lv_timer_resume(clock_timer);
                // Update immediately to show current time
                datetime_t current_time;
                PCF85063_Read_Time(&current_time);
                clock_update(current_time.hour, current_time.minute, current_time.second);
            }
        }
        else
        {
            lv_obj_add_flag(clock_face, LV_OBJ_FLAG_HIDDEN);
            // Pause the timer when hidden to save resources
            if (clock_timer) {
                lv_timer_pause(clock_timer);
            }
        }
    }
}

void clock_cleanup(void)
{
    // Stop the update timer
    if (clock_timer != NULL)
    {
        lv_timer_del(clock_timer);
        clock_timer = NULL;
    }

    if (hour_shadow)
        lv_obj_del(hour_shadow);
    if (minute_shadow)
        lv_obj_del(minute_shadow);
    if (hour_hand)
        lv_obj_del(hour_hand);
    if (minute_hand)
        lv_obj_del(minute_hand);
    if (clock_face)
        lv_obj_del(clock_face);

    hour_shadow = NULL;
    minute_shadow = NULL;
    hour_hand = NULL;
    minute_hand = NULL;
    clock_face = NULL;

    ESP_LOGD(TAG, "Clock cleaned up");
}
