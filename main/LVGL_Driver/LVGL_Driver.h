#pragma once
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "demos/lv_demos.h"

#include "ST77916.h"

// LVGL buffer size - 1/3 screen for smoother updates (120 lines x 360 pixels)
#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * (EXAMPLE_LCD_HEIGHT / 3))
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2

extern lv_disp_draw_buf_t disp_buf;                                                 // contains internal graphic buffer(s) called draw buffer(s)
extern lv_disp_drv_t disp_drv;                                                      // contains callback functions
extern lv_disp_t *disp;    

void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
void example_lvgl_port_update_callback(lv_disp_drv_t *drv);
void example_increase_lvgl_tick(void *arg);

void LVGL_Init(void);                     // Call this function to initialize the screen (must be called in the main function) !!!!!

/**
 * @brief Check for display errors and trigger refresh if needed
 * 
 * Call this periodically from the main loop. If SPI errors have been 
 * detected during display updates, this will invalidate the screen
 * to force a full redraw.
 * 
 * @return true if a refresh was triggered
 */
bool lvgl_check_and_refresh(void);