#include "LVGL_Driver.h"
#include "CST820.h"  // Always include - runtime detection via touch_available

static const char *TAG_LVGL = "LVGL";

// Periodic display refresh to prevent accumulated display corruption
// Note: SPI queue errors are logged internally by ESP-IDF but esp_lcd_panel_draw_bitmap()
// returns ESP_OK even when queue errors occur (async error), so we can't detect them.
// Instead, we do periodic preventive refreshes.
static uint32_t last_refresh_ms = 0;
#define PERIODIC_REFRESH_INTERVAL_MS 30000  // Force full refresh every 30 seconds
    

lv_disp_draw_buf_t disp_buf;                                                 // contains internal graphic buffer(s) called draw buffer(s)
lv_disp_drv_t disp_drv;                                                      // contains callback functions
lv_indev_drv_t indev_drv;

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}


void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    // Sync to vertical blanking on the first strip of each frame.
    // lv_disp_flush_is_last() is true for the very last strip of a frame,
    // so we use offsety1 == 0 as a proxy for "first strip".
    if (offsety1 == 0) {
        lcd_wait_te();
    }

    // copy a buffer's content to a specific area of the display
    // SPI completion callback will call lv_disp_flush_ready() when DMA transfer completes
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    // Don't call lv_disp_flush_ready here - SPI callback handles it
}

/*Read the touchpad - only called if touch_available is true at init time */
void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )
{
  static bool last_state = false;
  
  // Read real CST820 touch controller instead of simulated touch
  if (tp != NULL && touch_available) {
    uint16_t x[1], y[1];
    uint8_t point_num = 0;
    
    // Read touch data from CST820
    esp_lcd_touch_read_data(tp);
    
    // Get touch coordinates
    bool pressed = esp_lcd_touch_get_coordinates(tp, x, y, NULL, &point_num, 1);
    
    if (pressed && point_num > 0) {
      data->point.x = x[0];
      data->point.y = y[0];
      data->state = LV_INDEV_STATE_PR;
      if (!last_state) {
        last_state = true;
      }
    } else {
      data->state = LV_INDEV_STATE_REL;
      if (last_state) {
        last_state = false;
      }
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
void example_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
lv_disp_t *disp;
void LVGL_Init(void)
{
    ESP_LOGI(TAG_LVGL, "Initialize LVGL library");
    lv_init();
    
    // Double buffer in PSRAM — LVGL renders into one while SPI DMA sends the other.
    // PSRAM is Octal @ 80 MHz with DMA support on this board.
    size_t buf_bytes = LVGL_BUF_LEN * sizeof(lv_color_t);
    lv_color_t *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        // Fall back to single internal-DMA buffer if PSRAM alloc fails
        ESP_LOGW(TAG_LVGL, "PSRAM double-buffer alloc failed, falling back to single internal buffer");
        if (buf1) free(buf1);
        if (buf2) free(buf2);
        buf2 = NULL;
        buf_bytes = EXAMPLE_LCD_WIDTH * (EXAMPLE_LCD_HEIGHT / 10) * sizeof(lv_color_t);
        buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!buf1) {
            buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
        }
    }
    assert(buf1);
    ESP_LOGI(TAG_LVGL, "LVGL buffer: %d bytes x %s in %s",
             (int)buf_bytes, buf2 ? "2 (double)" : "1 (single)",
             buf2 ? "PSRAM" : "internal");
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf2 ? LVGL_BUF_LEN : (buf_bytes / sizeof(lv_color_t)));

    ESP_LOGI(TAG_LVGL, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_WIDTH;             
    disp_drv.ver_res = EXAMPLE_LCD_HEIGHT;                                                     // Horizontal pixel count
    // disp_drv.rotated = LV_DISP_ROT_90;                                                            // Vertical axis pixel count
    disp_drv.flush_cb = example_lvgl_flush_cb;                                                          // Function : copy a buffer's content to a specific area of the display
    disp_drv.drv_update_cb = example_lvgl_port_update_callback;                                         // Function : Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. 
    disp_drv.draw_buf = &disp_buf;                                                                      // LVGL will use this buffer(s) to draw the screens contents
    // disp_drv.full_refresh = 1;                                                                       // Disabled - use partial refresh with 1/10 screen buffers
    disp_drv.user_data = panel_handle;                
    ESP_LOGI(TAG_LVGL,"Register display indev to LVGL");                                                  // Custom display driver user data
    disp = lv_disp_drv_register(&disp_drv);
    
    // Enable SPI completion callbacks now that LVGL display driver is registered
    lcd_set_lvgl_ready();     
    
    // Only register touch input device if touch controller was detected
    if (touch_available) {
        lv_indev_drv_init ( &indev_drv );
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.disp = disp;
        indev_drv.read_cb = example_touchpad_read;
        lv_indev_drv_register( &indev_drv );
        ESP_LOGI(TAG_LVGL, "Touch input device registered");
    } else {
        ESP_LOGI(TAG_LVGL, "No touch - skipping touch input device registration");
    }

    /********************* LVGL *********************/
    ESP_LOGI(TAG_LVGL, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

}

/**
 * @brief Periodic display refresh to prevent accumulated corruption
 * 
 * Call this periodically from the main loop. Forces a full screen 
 * refresh at regular intervals to clean up any display shredding
 * caused by SPI queue errors (which we cannot detect via return codes).
 * 
 * @return true if a refresh was triggered
 */
bool lvgl_check_and_refresh(void)
{
    uint32_t now_ms = lv_tick_get();
    
    // Check for periodic preventive refresh
    if ((now_ms - last_refresh_ms) >= PERIODIC_REFRESH_INTERVAL_MS) {
        last_refresh_ms = now_ms;
        
        // Invalidate the entire screen to force redraw
        lv_obj_invalidate(lv_scr_act());
        
        ESP_LOGI(TAG_LVGL, "Periodic display refresh (every %d sec)", PERIODIC_REFRESH_INTERVAL_MS / 1000);
        return true;
    }
    
    return false;
}
