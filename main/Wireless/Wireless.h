/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "nvs_flash.h" 
#include "esp_log.h"

#include <stdio.h>
#include <string.h>  // For memcpy
#include "esp_system.h"
#include "esp_bt.h"
// NimBLE is used instead of Bluedroid - BLE scanning handled by BLE_TPMS module
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"



extern uint16_t BLE_NUM;
extern uint16_t WIFI_NUM;
extern bool Scan_finish;

void Wireless_Init(void);
void WIFI_Init(void *arg);
uint16_t WIFI_Scan(void);
void BLE_Init(void *arg);
uint16_t BLE_Scan(void);