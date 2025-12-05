#include "I2C_Driver.h"

static const char *SCANNER_TAG = "I2C_SCANNER";

void I2C_Scan(void)
{
    ESP_LOGI(SCANNER_TAG, "Scanning I2C bus...");
    
    int devices_found = 0;
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
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
