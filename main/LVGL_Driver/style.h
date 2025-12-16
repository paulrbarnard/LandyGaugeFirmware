/**
 * @file style.h
 * @brief LVGL style definitions for analog clock and UI elements
 */

#ifndef STYLE_H
#define STYLE_H

#include "lvgl.h"


// display size
#define DISP_W 360
#define DISP_H 360

// Color definitions for clock and UI
#define COLOR_BACKGROUND lv_color_make(20, 20, 20)
#define COLOR_FACE lv_color_make(40, 40, 40)
#define COLOR_WHITE lv_color_make(255, 255, 255)
#define COLOR_GREEN lv_color_make(0, 255, 0)

// Helper functions for hand and marker colors
static inline lv_color_t get_accent_color(bool night_mode)
{
    return night_mode ? COLOR_GREEN : COLOR_WHITE;
}

/**
 * @brief Create recessed shadow effect on a gauge container
 * 
 * Creates a 3D recessed appearance with a dark shadow (top-left) and
 * a light accent-colored shadow (bottom-right).
 * 
 * @param parent The parent container to add shadow objects to
 * @param night_mode Whether to use night mode accent color
 */
void create_gauge_shadows(lv_obj_t *parent, bool night_mode);

/**
 * @brief Create shadow effect with inverted positions (for horizon gauge)
 * 
 * Same as create_gauge_shadows but with swapped positions to correct
 * for visual inversion caused by certain background rendering.
 * 
 * @param parent The parent container to add shadow objects to
 * @param night_mode Whether to use night mode accent color
 */
void create_gauge_shadows_inverted(lv_obj_t *parent, bool night_mode);


#endif // STYLE_H
