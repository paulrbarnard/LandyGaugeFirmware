
#pragma once
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_intr_alloc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "lvgl.h"
#include "driver/ledc.h"

#include "esp_lcd_st77916.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"


#define EXAMPLE_LCD_WIDTH                   (360)
#define EXAMPLE_LCD_HEIGHT                  (360)
#define EXAMPLE_LCD_COLOR_BITS              (16)

// Touch controller I2C bus (separate from other I2C devices)
#define I2C_TOUCH_NUM                       1              // I2C bus 1 for touch
#define I2C_TOUCH_SCL_IO                    GPIO_NUM_3     // TP_SCL on GPIO3
#define I2C_TOUCH_SDA_IO                    GPIO_NUM_1     // TP_SDA on GPIO1
#define I2C_TOUCH_FREQ_HZ                   400000         // 400kHz

// Touch controller pins (CST816)
#define I2C_Touch_INT_IO                    (GPIO_NUM_4)   // TP_INT on GPIO4
#define I2C_Touch_RST_IO                    (GPIO_NUM_NC)  // TP_RST via EXIO1, not direct GPIO

#define ESP_PANEL_HOST_SPI_ID_DEFAULT       (SPI2_HOST)
#define ESP_PANEL_LCD_SPI_MODE              (0)
#define ESP_PANEL_LCD_SPI_CLK_HZ            (80 * 1000 * 1000)  // 80MHz - full speed
#define ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ    (10)                // Queue depth for async transfers
#define ESP_PANEL_LCD_SPI_CMD_BITS          (32)
#define ESP_PANEL_LCD_SPI_PARAM_BITS        (8)

#define ESP_PANEL_LCD_SPI_IO_TE             (18)
#define ESP_PANEL_LCD_SPI_IO_SCK            (40)
#define ESP_PANEL_LCD_SPI_IO_DATA0          (46)
#define ESP_PANEL_LCD_SPI_IO_DATA1          (45)
#define ESP_PANEL_LCD_SPI_IO_DATA2          (42)
#define ESP_PANEL_LCD_SPI_IO_DATA3          (41)
#define ESP_PANEL_LCD_SPI_IO_CS             (21)
#define EXAMPLE_LCD_PIN_NUM_RST             (-1)    // Controlled via EXIO2
#define EXAMPLE_LCD_PIN_NUM_BK_LIGHT        (5)

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL       (1)
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL      (!EXAMPLE_LCD_BK_LIGHT_ON_LEVEL)

#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE   (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT * sizeof(uint16_t))

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_GPIO       EXAMPLE_LCD_PIN_NUM_BK_LIGHT
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_TEST_DUTY         (4000)
#define LEDC_ResolutionRatio   LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty          ((1 << LEDC_ResolutionRatio) - 1)
#define Backlight_MAX          100

extern esp_lcd_panel_handle_t panel_handle;
extern uint8_t LCD_Backlight;

void ST77916_Init(void);
void LCD_Init(void);

/**
 * @brief Set the LVGL display driver for SPI completion callback
 * MUST be called BEFORE LCD_Init() to enable proper SPI/LVGL synchronization.
 * This prevents SPI queue overflow when BLE is active.
 * @param drv Pointer to the LVGL display driver
 */
void lcd_set_lvgl_disp_drv(lv_disp_drv_t *drv);

/**
 * @brief Mark LVGL as ready to receive flush_ready callbacks
 * MUST be called AFTER lv_disp_drv_register() completes in LVGL_Init()
 */
void lcd_set_lvgl_ready(void);

void Backlight_Init(void);
void Set_Backlight(uint8_t Light);
void ST77916_FillScreen(uint16_t color);

/**
 * @brief Wait for the next TE (tearing effect) pulse from the display.
 * Blocks up to 20 ms. No-op if TE was not initialised.
 */
void lcd_wait_te(void);

