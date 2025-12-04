/**
 * @file clock.c
 * @brief Analog clock display implementation using LVGL
 */

#include "clock.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "CLOCK";

// Display mode
static bool night_mode = true;  // Default to night mode for testing

// Color helper function
static inline lv_color_t get_primary_color(void) {
    return night_mode ? lv_color_make(0, 255, 0) : lv_color_white();  // Green for night, white for day
}

// Clock dimensions
#define CLOCK_SIZE          360     // Clock diameter - full screen
#define CLOCK_CENTER_X      180     // Screen center X (480/2)
#define CLOCK_CENTER_Y      180     // Screen center Y (480/2)
#define HOUR_HAND_LENGTH    110     // Lengthened by 10 from 100
#define MINUTE_HAND_LENGTH  165     // Lengthened by 5 from 160
#define CENTER_DOT_SIZE     85      // Reduced by 5 from 90

// LVGL objects
static lv_obj_t *clock_face = NULL;
static lv_obj_t *hour_hand = NULL;
static lv_obj_t *minute_hand = NULL;

// Hand points (line coordinates)
static lv_point_t hour_points[2];
static lv_point_t minute_points[2];

/**
 * @brief Draw clock face with hour markers and numbers
 */
static void draw_clock_face(void)
{
    lv_obj_t *scr = lv_scr_act();
    
    // Clean screen and set very dark grey background
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_make(20, 20, 20), 0);
    
    // Create shadow effect for recessed appearance (dark top-left shadow)
    // Make circle smaller (330px) so 15px shadow fits within 360px screen
    lv_obj_t *shadow_dark = lv_obj_create(scr);
    lv_obj_set_size(shadow_dark, CLOCK_SIZE - 30, CLOCK_SIZE - 30);
    lv_obj_set_pos(shadow_dark, CLOCK_CENTER_X - (CLOCK_SIZE - 30)/2, CLOCK_CENTER_Y - (CLOCK_SIZE - 30)/2);
    lv_obj_set_style_radius(shadow_dark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_dark, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_border_width(shadow_dark, 0, 0);
    
    // Dark shadow from top-left (inset appearance)
    lv_obj_set_style_shadow_width(shadow_dark, 15, 0);
    lv_obj_set_style_shadow_opa(shadow_dark, LV_OPA_70, 0);
    lv_obj_set_style_shadow_color(shadow_dark, lv_color_black(), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_dark, -4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_dark, -4, 0);
    
    // Create light highlight for recessed appearance (light bottom-right)
    lv_obj_t *shadow_light = lv_obj_create(scr);
    lv_obj_set_size(shadow_light, CLOCK_SIZE - 30, CLOCK_SIZE - 30);
    lv_obj_set_pos(shadow_light, CLOCK_CENTER_X - (CLOCK_SIZE - 30)/2, CLOCK_CENTER_Y - (CLOCK_SIZE - 30)/2);
    lv_obj_set_style_radius(shadow_light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_light, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_border_width(shadow_light, 0, 0);
    
    // Light highlight from bottom-right (to complete sunken effect)
    lv_obj_set_style_shadow_width(shadow_light, 15, 0);
    lv_obj_set_style_shadow_opa(shadow_light, LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(shadow_light, get_primary_color(), 0);  // Green in night mode
    lv_obj_set_style_shadow_ofs_x(shadow_light, 4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_light, 4, 0);
    
    // Draw hour markers and numbers
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * M_PI / 180.0;  // Convert to radians, -90 to start at 12
        
        // Determine marker length: longer for 12, 3, 6, 9 positions
        int marker_start, marker_end;
        int line_width;
        
        if (i == 0 || i == 3 || i == 6 || i == 9) {
            // Long markers for 12, 3, 6, 9 - leave 20px for shadow at edge
            marker_start = CLOCK_SIZE / 2 - 27 - 15;
            marker_end = CLOCK_SIZE / 2 - 4 - 15;
            line_width = 8;  // Quarter hour markers
        } else {
            // Short markers for other hours - leave 20px for shadow at edge
            marker_start = CLOCK_SIZE / 2 - 27 - 15;
            marker_end = CLOCK_SIZE / 2 - 4 - 15;
            line_width = 4;  // Intermediate hour markers
        }
        
        lv_obj_t *marker = lv_line_create(scr);
        lv_point_t *marker_points = malloc(2 * sizeof(lv_point_t));
        marker_points[0].x = CLOCK_CENTER_X + cos(angle) * marker_start;
        marker_points[0].y = CLOCK_CENTER_Y + sin(angle) * marker_start;
        marker_points[1].x = CLOCK_CENTER_X + cos(angle) * marker_end;
        marker_points[1].y = CLOCK_CENTER_Y + sin(angle) * marker_end;
        
        lv_line_set_points(marker, marker_points, 2);
        lv_obj_set_style_line_width(marker, line_width, 0);
        lv_obj_set_style_line_color(marker, get_primary_color(), 0);  // Green in night mode
        lv_obj_set_style_line_rounded(marker, false, 0);  // Square ends
        
        // Draw numbers at 12, 3, 6, 9
        if (i == 0 || i == 3 || i == 6 || i == 9) {
            int number_radius = CLOCK_SIZE / 2 - 45 - 15;  // Leave 20px for shadow
            lv_obj_t *num_label = lv_label_create(scr);
            
            const char *num_text;
            switch(i) {
                case 0:  num_text = "12"; break;
                case 3:  num_text = "3"; break;
                case 6:  num_text = "6"; break;
                case 9:  num_text = "9"; break;
                default: num_text = ""; break;
            }
            
            lv_label_set_text(num_label, num_text);
            lv_obj_set_style_text_color(num_label, get_primary_color(), 0);  // Green in night mode
            lv_obj_set_style_text_font(num_label, &lv_font_montserrat_42, 0);
            
            // Position number
            int num_x = CLOCK_CENTER_X + cos(angle) * number_radius;
            int num_y = CLOCK_CENTER_Y + sin(angle) * number_radius;
            lv_obj_align(num_label, LV_ALIGN_CENTER, 
                        num_x - CLOCK_CENTER_X, 
                        num_y - CLOCK_CENTER_Y);
        }
    }
}

/**
 * @brief Create clock hands (lines)
 */
static void create_hands(void)
{
    lv_obj_t *scr = lv_scr_act();
    
    // Create hour hand
    hour_hand = lv_line_create(scr);
    lv_obj_set_style_line_width(hour_hand, 12, 0);  // 2x thicker (was 8)
    lv_obj_set_style_line_color(hour_hand, get_primary_color(), 0);  // Green in night mode
    lv_obj_set_style_line_rounded(hour_hand, true, 0);
    
    // Create minute hand
    minute_hand = lv_line_create(scr);
    lv_obj_set_style_line_width(minute_hand, 9, 0);  // 2x thicker (was 6)
    lv_obj_set_style_line_color(minute_hand, get_primary_color(), 0);  // Green in night mode
    lv_obj_set_style_line_rounded(minute_hand, true, 0);
    
    // Create center cap (raised button appearance) - created last to be on top
    lv_obj_t *center = lv_obj_create(scr);
    lv_obj_set_size(center, CENTER_DOT_SIZE, CENTER_DOT_SIZE);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    
    // Create gradient effect - dark grey to black, rotated 45 degrees counter-clockwise
    lv_obj_set_style_bg_color(center, lv_color_make(60, 60, 60), 0);  // Lighter (top-right)
    lv_obj_set_style_bg_grad_color(center, lv_color_black(), 0);      // Darker (bottom-left)
    lv_obj_set_style_bg_grad_dir(center, LV_GRAD_DIR_HOR, 0);         // Horizontal gradient
    
    // Add subtle highlight shadow from top-left for enhanced raised effect
    lv_obj_set_style_shadow_width(center, 10, 0);
    lv_obj_set_style_shadow_opa(center, LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(center, get_primary_color(), 0);  // Green in night mode
    lv_obj_set_style_shadow_ofs_x(center, -3, 0);
    lv_obj_set_style_shadow_ofs_y(center, -3, 0);
    
    lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
}

void clock_init(void)
{
    ESP_LOGI(TAG, "Initializing analog clock");
    
    // Draw the clock face
    draw_clock_face();
    
    // Create the hands
    create_hands();
    
    // Initialize to 12:00:00
    clock_update(0, 0, 0);
    
    ESP_LOGI(TAG, "Clock initialized");
}

void clock_update(uint8_t hour, uint8_t minute, uint8_t second)
{
    // Calculate angles (0 degrees = 12 o'clock, clockwise)
    // Subtract 90 to start at 12 o'clock position
    float hour_angle = ((hour % 12) * 30 + minute * 0.5 - 90) * M_PI / 180.0;
    float minute_angle = (minute * 6 + second * 0.1 - 90) * M_PI / 180.0;
    
    // Update hour hand
    hour_points[0].x = CLOCK_CENTER_X;
    hour_points[0].y = CLOCK_CENTER_Y;
    hour_points[1].x = CLOCK_CENTER_X + cos(hour_angle) * HOUR_HAND_LENGTH;
    hour_points[1].y = CLOCK_CENTER_Y + sin(hour_angle) * HOUR_HAND_LENGTH;
    lv_line_set_points(hour_hand, hour_points, 2);
    
    // Update minute hand
    minute_points[0].x = CLOCK_CENTER_X;
    minute_points[0].y = CLOCK_CENTER_Y;
    minute_points[1].x = CLOCK_CENTER_X + cos(minute_angle) * MINUTE_HAND_LENGTH;
    minute_points[1].y = CLOCK_CENTER_Y + sin(minute_angle) * MINUTE_HAND_LENGTH;
    lv_line_set_points(minute_hand, minute_points, 2);
}

void clock_set_night_mode(bool is_night_mode)
{
    if (night_mode != is_night_mode) {
        night_mode = is_night_mode;
        ESP_LOGI(TAG, "Clock mode changed to: %s", night_mode ? "NIGHT (green)" : "DAY (white)");
        // Redraw the entire clock with new colors
        draw_clock_face();
        create_hands();
    }
}

void clock_set_visible(bool visible)
{
    if (clock_face) {
        if (visible) {
            lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(clock_face, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void clock_cleanup(void)
{
    if (hour_hand) lv_obj_del(hour_hand);
    if (minute_hand) lv_obj_del(minute_hand);
    if (clock_face) lv_obj_del(clock_face);
    
    hour_hand = NULL;
    minute_hand = NULL;
    clock_face = NULL;
    
    ESP_LOGI(TAG, "Clock cleaned up");
}
