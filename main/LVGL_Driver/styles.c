/**
 * @file styles.c
 * @brief LVGL style helper functions for gauge effects
 */

#include "style.h"
#include "esp_log.h"

static const char *TAG = "STYLES";

// Default shadow parameters
#define DEFAULT_SHADOW_WIDTH 10
#define DEFAULT_SHADOW_OFFSET 13

/**
 * @brief Create recessed shadow effect on a gauge container
 * 
 * Creates a 3D recessed appearance with a dark shadow (top-left) and
 * a light accent-colored shadow (bottom-right).
 * 
 * @param parent The parent container to add shadow objects to
 * @param night_mode Whether to use night mode accent color
 */
void create_gauge_shadows(lv_obj_t *parent, bool night_mode)
{
    if (!parent) {
        ESP_LOGE(TAG, "create_gauge_shadows: parent is NULL");
        return;
    }

    // Create shadow effect for recessed appearance (dark top-left shadow)
    // lv_obj_t *shadow_dark = lv_obj_create(parent);
    // lv_obj_set_size(shadow_dark, DISP_W + DEFAULT_SHADOW_WIDTH, DISP_H + DEFAULT_SHADOW_WIDTH);
    // lv_obj_set_style_radius(shadow_dark, LV_RADIUS_CIRCLE, 0);
    // lv_obj_set_style_bg_color(shadow_dark, COLOR_FACE, 0);
    // lv_obj_set_style_bg_opa(shadow_dark, LV_OPA_TRANSP, 0);
    // lv_obj_set_style_border_width(shadow_dark, 1, 0);
    // lv_obj_set_style_border_color(shadow_dark, COLOR_FACE, 0);
    // lv_obj_align(shadow_dark, LV_ALIGN_TOP_LEFT, -DEFAULT_SHADOW_OFFSET, -DEFAULT_SHADOW_OFFSET);
    // lv_obj_set_style_shadow_width(shadow_dark, DEFAULT_SHADOW_WIDTH, 0);
    // lv_obj_set_style_shadow_opa(shadow_dark, LV_OPA_70, 0);
    // lv_obj_set_style_shadow_color(shadow_dark, lv_color_black(), 0);
    // lv_obj_set_style_shadow_ofs_x(shadow_dark, 4, 0);
    // lv_obj_set_style_shadow_ofs_y(shadow_dark, 4, 0);
    // ESP_LOGI(TAG, "shadow_dark created");

    // Create light shadow effect (bottom-right accent glow)
    lv_obj_t *shadow_light = lv_obj_create(parent);
    lv_obj_set_size(shadow_light, DISP_W + DEFAULT_SHADOW_WIDTH, DISP_H + DEFAULT_SHADOW_WIDTH);
    lv_obj_set_style_radius(shadow_light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_light, COLOR_FACE, 0);
    lv_obj_set_style_bg_opa(shadow_light, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shadow_light, 1, 0);
    lv_obj_set_style_border_color(shadow_light, COLOR_FACE, 0);
    lv_obj_align(shadow_light, LV_ALIGN_BOTTOM_RIGHT, DEFAULT_SHADOW_OFFSET, DEFAULT_SHADOW_OFFSET);
    lv_obj_set_style_shadow_width(shadow_light, DEFAULT_SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_spread(shadow_light, DEFAULT_SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_opa(shadow_light, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(shadow_light, get_accent_color(night_mode), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_light, -4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_light, -4, 0);
    ESP_LOGI(TAG, "shadow_light created");
}

/**
 * @brief Create shadow effect with inverted positions (for horizon gauge)
 * 
 * Same as create_gauge_shadows but with swapped positions to correct
 * for visual inversion caused by certain background rendering.
 * 
 * @param parent The parent container to add shadow objects to
 * @param night_mode Whether to use night mode accent color
 */
void create_gauge_shadows_inverted(lv_obj_t *parent, bool night_mode)
{
    if (!parent) {
        ESP_LOGE(TAG, "create_gauge_shadows_inverted: parent is NULL");
        return;
    }

    // Create dark shadow at BOTTOM-RIGHT (inverted from normal)
    lv_obj_t *shadow_dark = lv_obj_create(parent);
    lv_obj_set_size(shadow_dark, DISP_W + DEFAULT_SHADOW_WIDTH, DISP_H + DEFAULT_SHADOW_WIDTH);
    lv_obj_set_style_radius(shadow_dark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_dark, COLOR_FACE, 0);
    lv_obj_set_style_bg_opa(shadow_dark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shadow_dark, 1, 0);
    lv_obj_set_style_border_color(shadow_dark, COLOR_FACE, 0);
    lv_obj_align(shadow_dark, LV_ALIGN_BOTTOM_RIGHT, DEFAULT_SHADOW_OFFSET, DEFAULT_SHADOW_OFFSET);
    lv_obj_set_style_shadow_width(shadow_dark, DEFAULT_SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_opa(shadow_dark, LV_OPA_70, 0);
    lv_obj_set_style_shadow_color(shadow_dark, lv_color_black(), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_dark, -4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_dark, -4, 0);
    ESP_LOGI(TAG, "shadow_dark created (inverted)");

    // Create light shadow at TOP-LEFT (inverted from normal)
    lv_obj_t *shadow_light = lv_obj_create(parent);
    lv_obj_set_size(shadow_light, DISP_W + DEFAULT_SHADOW_WIDTH, DISP_H + DEFAULT_SHADOW_WIDTH);
    lv_obj_set_style_radius(shadow_light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_light, COLOR_FACE, 0);
    lv_obj_set_style_bg_opa(shadow_light, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shadow_light, 1, 0);
    lv_obj_set_style_border_color(shadow_light, COLOR_FACE, 0);
    lv_obj_align(shadow_light, LV_ALIGN_TOP_LEFT, -DEFAULT_SHADOW_OFFSET, -DEFAULT_SHADOW_OFFSET);
    lv_obj_set_style_shadow_width(shadow_light, DEFAULT_SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_spread(shadow_light, DEFAULT_SHADOW_WIDTH, 0);
    lv_obj_set_style_shadow_opa(shadow_light, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(shadow_light, get_accent_color(night_mode), 0);
    lv_obj_set_style_shadow_ofs_x(shadow_light, 4, 0);
    lv_obj_set_style_shadow_ofs_y(shadow_light, 4, 0);
    ESP_LOGI(TAG, "shadow_light created (inverted)");
}
