/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

#include "I2C_Driver.h"

static const char *SCANNER_TAG = "I2C_SCANNER";

void I2C_Scan(void)
{
    ESP_LOGI(SCANNER_TAG, "Scanning I2C bus...");

    int devices_found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        if (I2C_Probe(addr, 50) == ESP_OK) {
            ESP_LOGI(SCANNER_TAG, "Found device at address 0x%02X", addr);
            devices_found++;
        }
    }

    if (devices_found == 0) {
        ESP_LOGW(SCANNER_TAG, "No I2C devices found!");
    } else {
        ESP_LOGI(SCANNER_TAG, "Scan complete. Found %d device(s)", devices_found);
    }
}
