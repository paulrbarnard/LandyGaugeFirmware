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

static const char *TAG = "CLOCK";

// Display mode - can be changed at runtime
static bool night_mode = false; // Day mode (white hands)

// Color definitions - Note: Display expects BGR format, so R and B are swapped
// lv_color_make(R, G, B) but display reads as BGR
#define COLOR_BACKGROUND lv_color_make(20, 20, 20) // Very dark grey (equal RGB = grey)
#define COLOR_FACE lv_color_make(40, 40, 40)       // Dark grey (equal RGB = grey)
#define COLOR_WHITE lv_color_make(255, 255, 255)   // White (equal RGB = white)
#define COLOR_GREEN lv_color_make(0, 255, 0)       // Green

// Color helper functions
static inline lv_color_t get_hand_color(void)
{
    return night_mode ? COLOR_GREEN : COLOR_WHITE;
}

static inline lv_color_t get_marker_color(void)
{
    return night_mode ? COLOR_GREEN : COLOR_WHITE;
}

// Clock dimensions
#define CLOCK_SIZE 360                  // Clock diameter
#define TICK_HOFF 20                    // Horizontal offset for tick centering
#define TICK_YOFF 20                    // Vertical offset for tick centering
#define CLOCK_CENTER_X (CLOCK_SIZE / 2) // Container center X
#define CLOCK_CENTER_Y (CLOCK_SIZE / 2) // Container center Y
#define HOUR_HAND_LENGTH 84             // 70% of minute hand length, -5 for clearance
#define MINUTE_HAND_LENGTH 122          // Reaches inside of hour tick marks, -5 for clearance
#define CENTER_DOT_SIZE 64              // 25% smaller from 85
#define SHADOW_WIDTH 10                 // Shadow width for 3D effects
#define SHADOW_OFFSET 13                 // Shadow offset for 3D effects

// LVGL objects
static lv_obj_t *clock_face = NULL;
static lv_obj_t *hour_hand = NULL;
static lv_obj_t *minute_hand = NULL;

// Update timer
static lv_timer_t *clock_timer = NULL;

// Hand points (line coordinates)
static lv_point_t hour_points[2];
static lv_point_t minute_points[2];

#define DISP_W 360
#define DISP_H 360

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
        ESP_LOGI(TAG, "clock_face created and centered");
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
        lv_obj_set_style_line_color(marker, get_marker_color(), 0);
        lv_obj_set_style_line_rounded(marker, false, 0);
        ESP_LOGI(TAG, "Tick mark %d created at (%d,%d)->(%d,%d)", i, marker_points[0].x, marker_points[0].y, marker_points[1].x, marker_points[1].y);
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
            lv_obj_set_style_text_color(num_label, get_marker_color(), 0);
            lv_obj_set_style_text_font(num_label, &lv_font_montserrat_32, 0);
            int num_x = center_x + cos(angle) * number_radius;
            int num_y = center_y + sin(angle) * number_radius;
            lv_obj_align(num_label, LV_ALIGN_CENTER,
                         num_x - center_x - 2,
                         num_y - center_y - 4);
            ESP_LOGI(TAG, "Hour number %s created at (%d,%d)", num_text, num_x, num_y);
              // SAFE TO FREE — LVGL makes its own copy
            //free(marker_points);

        }
    }

    // Create shadow effect for recessed appearance (dark top-left shadow)
     lv_obj_t *shadow_dark = lv_obj_create(clock_face);
     lv_obj_set_size(shadow_dark, CLOCK_SIZE + SHADOW_WIDTH, CLOCK_SIZE + SHADOW_WIDTH);
     lv_obj_set_style_radius(shadow_dark, LV_RADIUS_CIRCLE, 0);
     lv_obj_set_style_bg_color(shadow_dark, COLOR_FACE, 0);
     lv_obj_set_style_bg_opa(shadow_dark, LV_OPA_TRANSP, 0);   // make the main body transparent
    lv_obj_set_style_border_width(shadow_dark, 1, 0);
     lv_obj_set_style_border_color(shadow_dark, COLOR_FACE, 0);
     lv_obj_align(shadow_dark, LV_ALIGN_TOP_LEFT, -SHADOW_OFFSET, -SHADOW_OFFSET);
     lv_obj_set_style_shadow_width(shadow_dark, SHADOW_WIDTH, 0);
     lv_obj_set_style_shadow_opa(shadow_dark, LV_OPA_70, 0);
     lv_obj_set_style_shadow_color(shadow_dark, lv_color_black(), 0);
     lv_obj_set_style_shadow_ofs_x(shadow_dark, 4, 0);
     lv_obj_set_style_shadow_ofs_y(shadow_dark, 4, 0);
     ESP_LOGI(TAG, "shadow_dark created");



    lv_obj_t *shadow_light = lv_obj_create(clock_face);
    lv_obj_set_size(shadow_light, CLOCK_SIZE + SHADOW_WIDTH, CLOCK_SIZE + SHADOW_WIDTH);
    lv_obj_set_style_radius(shadow_light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_light, COLOR_FACE, 0);
    lv_obj_set_style_bg_opa(shadow_light, LV_OPA_TRANSP, 0);   // make the main body transparent
    lv_obj_set_style_border_width(shadow_light, 1, 0);
    lv_obj_set_style_border_color(shadow_light, COLOR_FACE, 0);
    lv_obj_align(shadow_light, LV_ALIGN_BOTTOM_RIGHT,SHADOW_OFFSET, SHADOW_OFFSET);
    //lv_obj_align(shadow_light, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_shadow_width(shadow_light, SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_spread(shadow_light, SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_opa(shadow_light, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(shadow_light, get_hand_color(), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_light, -4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_light, -4, 0);
    ESP_LOGI(TAG, "shadow_light created");

  }

/**
 * @brief Create clock hands (lines)
 */
static void create_hands(void)
{
    // Create hour hand
    hour_hand = lv_line_create(clock_face);
    lv_obj_set_style_line_width(hour_hand, 12, 0); // 2x thicker (was 8)
    lv_obj_set_style_line_color(hour_hand, get_hand_color(), 0);
    lv_obj_set_style_line_rounded(hour_hand, true, 0);

    // Create minute hand
    minute_hand = lv_line_create(clock_face);
    lv_obj_set_style_line_width(minute_hand, 9, 0); // 2x thicker (was 6)
    lv_obj_set_style_line_color(minute_hand, get_hand_color(), 0);
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
    lv_obj_set_style_shadow_color(center, COLOR_WHITE, 0);
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
    ESP_LOGI(TAG, "Initializing analog clock");

    // Draw the clock face
    draw_clock_face();

    // Create the hands
    create_hands();

    // Read initial time from RTC and display it
    datetime_t current_time;
    PCF85063_Read_Time(&current_time);
    clock_update(current_time.hour, current_time.minute, current_time.second);
    ESP_LOGI(TAG, "Initial time set to %02d:%02d:%02d",
             current_time.hour, current_time.minute, current_time.second);

    // Create LVGL timer to update clock every second (1000ms)
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    if (clock_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create clock timer");
        return;
    }

    ESP_LOGI(TAG, "Clock initialized with LVGL timer");
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
        ESP_LOGI(TAG, "Clock mode changed to: %s", night_mode ? "NIGHT (green)" : "DAY (white)");
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
        }
        else
        {
            lv_obj_add_flag(clock_face, LV_OBJ_FLAG_HIDDEN);
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

    if (hour_hand)
        lv_obj_del(hour_hand);
    if (minute_hand)
        lv_obj_del(minute_hand);
    if (clock_face)
        lv_obj_del(clock_face);

    hour_hand = NULL;
    minute_hand = NULL;
    clock_face = NULL;

    ESP_LOGI(TAG, "Clock cleaned up");
}
