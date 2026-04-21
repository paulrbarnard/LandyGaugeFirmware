/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file
 * @brief ESP LCD touch: CST820
 */

#pragma once
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "ST77916.h"


/**
 * @brief Create a new CST820 touch driver
 *
 * @note  The I2C communication should be initialized before use this function.
 *
 * @param io LCD panel IO handle, it should be created by `esp_lcd_new_panel_io_i2c()`
 * @param config Touch panel configuration
 * @param tp Touch panel handle
 * @return
 *      - ESP_OK: on success
 */
esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp);

/**
 * @brief I2C address of the CST820 controller
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_CST820_ADDRESS    (0x15)

/**
 * @brief Touch IO configuration structure
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG()             \
    {                                                    \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST820_ADDRESS, \
        .control_phase_bytes = 1,                        \
        .dc_bit_offset = 0,                              \
        .lcd_cmd_bits = 8,                               \
        .flags =                                         \
        {                                                \
            .disable_control_phase = 1,                  \
        }                                                \
    }

// Note: I2C_Touch_INT_IO and I2C_Touch_RST_IO are defined in ST77916.h
// GPIO4 for touch interrupt, EXIO1 for touch reset (via TCA9554PWR)

extern esp_lcd_touch_handle_t tp;
extern bool touch_available;  // Runtime flag indicating if touch controller was detected

/**
 * @brief Initialize the touch controller
 * @return true if touch controller detected and initialized, false otherwise
 */
bool Touch_Init(void);
