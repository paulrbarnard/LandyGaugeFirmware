/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"


/********************* I2C *********************/
#define I2C_SCL_IO                  10
#define I2C_SDA_IO                  11
#define I2C_MASTER_NUM              0
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_TIMEOUT_MS       1000

void I2C_Init(void);

esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
esp_err_t I2C_Probe(uint8_t addr, int timeout_ms);
esp_err_t I2C_AddDeviceFreq(uint8_t addr, uint32_t scl_speed_hz);
i2c_master_bus_handle_t I2C_GetBusHandle(void);
