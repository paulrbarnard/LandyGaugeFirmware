#include "tilt.h"
#include "lvgl.h"
#include "LVGL_Driver/style.h"

static lv_obj_t *tilt_gauge = NULL;

void tilt_init(void) {
    // // Erase the LCD with a full fill of the dark grey face background (only once at init)
    // lv_obj_t *bg_fill = lv_obj_create(lv_scr_act());
    // lv_obj_set_size(bg_fill, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
    // lv_obj_set_style_bg_color(bg_fill, lv_color_make(20, 20, 20), 0);
    // lv_obj_set_style_border_width(bg_fill, 0, 0);
    // lv_obj_set_style_radius(bg_fill, 0, 0);
    // lv_obj_set_style_bg_opa(bg_fill, LV_OPA_COVER, 0);
    // lv_obj_center(bg_fill);

    // Create a base object similar to the clock, but no hands or numbers
    tilt_gauge = lv_obj_create(lv_scr_act());
    lv_obj_set_size(tilt_gauge, 240, 240); // Adjust as needed
    lv_obj_center(tilt_gauge);
    // Make the gauge fully transparent and empty
    lv_obj_set_style_bg_opa(tilt_gauge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tilt_gauge, 0, 0);
    lv_obj_set_style_radius(tilt_gauge, 0, 0);
}

void tilt_set_visible(bool visible) {
    if (tilt_gauge) {
        if (visible) {
            lv_obj_clear_flag(tilt_gauge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tilt_gauge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
