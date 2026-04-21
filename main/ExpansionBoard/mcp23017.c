/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file mcp23017.c
 * @brief MCP23017 16-bit I2C I/O Expander driver implementation
 *
 * Uses the project's I2C_Driver for bus communication.
 * Port A is configured as 8 outputs (drives AOZ1304N MOSFETs for relays).
 * Port B is configured as 8 inputs with polarity inversion (EL817S1
 * opto-couplers pull pins LOW when active, IPOL corrects this).
 */

#include "mcp23017.h"
#include "I2C_Driver.h"
#include "esp_log.h"

static const char *TAG = "MCP23017";

/*******************************************************************************
 * Low-level register access
 ******************************************************************************/

esp_err_t mcp23017_write_reg(uint8_t reg, uint8_t data)
{
    esp_err_t ret = I2C_Write(MCP23017_I2C_ADDR, reg, &data, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t mcp23017_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t ret = I2C_Read(MCP23017_I2C_ADDR, reg, data, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/*******************************************************************************
 * Port configuration
 ******************************************************************************/

esp_err_t mcp23017_set_port_direction(char port, uint8_t dir_mask)
{
    uint8_t reg = (port == 'B') ? MCP23017_REG_IODIRB : MCP23017_REG_IODIRA;
    return mcp23017_write_reg(reg, dir_mask);
}

esp_err_t mcp23017_set_port_pullups(char port, uint8_t pullup_mask)
{
    uint8_t reg = (port == 'B') ? MCP23017_REG_GPPUB : MCP23017_REG_GPPUA;
    return mcp23017_write_reg(reg, pullup_mask);
}

esp_err_t mcp23017_set_port_polarity(char port, uint8_t ipol_mask)
{
    uint8_t reg = (port == 'B') ? MCP23017_REG_IPOLB : MCP23017_REG_IPOLA;
    return mcp23017_write_reg(reg, ipol_mask);
}

/*******************************************************************************
 * GPIO read/write
 ******************************************************************************/

esp_err_t mcp23017_read_port(char port, uint8_t *value)
{
    uint8_t reg = (port == 'B') ? MCP23017_REG_GPIOB : MCP23017_REG_GPIOA;
    return mcp23017_read_reg(reg, value);
}

int mcp23017_read_pin(char port, uint8_t pin)
{
    if (pin > 7) {
        ESP_LOGE(TAG, "Invalid pin %d (must be 0-7)", pin);
        return -1;
    }
    uint8_t value = 0;
    esp_err_t ret = mcp23017_read_port(port, &value);
    if (ret != ESP_OK) return -1;
    return (value >> pin) & 0x01;
}

esp_err_t mcp23017_write_port(char port, uint8_t value)
{
    uint8_t reg = (port == 'B') ? MCP23017_REG_OLATB : MCP23017_REG_OLATA;
    return mcp23017_write_reg(reg, value);
}

esp_err_t mcp23017_write_pin(char port, uint8_t pin, bool state)
{
    if (pin > 7) {
        ESP_LOGE(TAG, "Invalid pin %d (must be 0-7)", pin);
        return ESP_ERR_INVALID_ARG;
    }

    // Read current output latch, modify, write back
    uint8_t reg = (port == 'B') ? MCP23017_REG_OLATB : MCP23017_REG_OLATA;
    uint8_t current = 0;
    esp_err_t ret = mcp23017_read_reg(reg, &current);
    if (ret != ESP_OK) return ret;

    if (state) {
        current |= (1 << pin);
    } else {
        current &= ~(1 << pin);
    }

    return mcp23017_write_reg(reg, current);
}

/*******************************************************************************
 * Initialization
 ******************************************************************************/

esp_err_t mcp23017_init(void)
{
    esp_err_t ret;

    // Verify chip is present by reading IOCON register
    uint8_t iocon = 0;
    ret = mcp23017_read_reg(MCP23017_REG_IOCON, &iocon);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCP23017 not found at I2C address 0x%02X", MCP23017_I2C_ADDR);
        return ret;
    }
    ESP_LOGI(TAG, "MCP23017 detected at 0x%02X (IOCON=0x%02X)", MCP23017_I2C_ADDR, iocon);

    // Configure IOCON: sequential operation enabled (SEQOP=0), BANK=0 (default)
    ret = mcp23017_write_reg(MCP23017_REG_IOCON, 0x00);
    if (ret != ESP_OK) return ret;

    // Port A: All 8 pins as outputs (0x00) — drives AOZ1304N MOSFETs for relays
    ret = mcp23017_set_port_direction('A', 0x00);
    if (ret != ESP_OK) return ret;

    // Port A: Outputs default low
    ret = mcp23017_write_port('A', 0x00);
    if (ret != ESP_OK) return ret;

    // Port B: All 8 pins as inputs (0xFF) — EL817S1 opto-coupler outputs
    ret = mcp23017_set_port_direction('B', 0xFF);
    if (ret != ESP_OK) return ret;

    // Port B: Polarity inversion on ALL pins (0xFF)
    // All opto-coupler inputs: active signal → opto ON → pin pulled LOW → IPOL inverts to 1
    // Fan inputs (GPB3, GPB4) are also inverted here; their thermo-switch logic
    // (active-low = grounded when fan on) is corrected in expansion_board.c after read.
    // This ensures the idle state (no signal, internal pullup HIGH) reads as 0 for ALL inputs.
    ret = mcp23017_set_port_polarity('B', 0xFF);
    if (ret != ESP_OK) return ret;

    // Port B: Enable internal pull-ups (0xFF)
    // Pulls pins HIGH when opto-coupler is off (phototransistor not conducting)
    ret = mcp23017_set_port_pullups('B', 0xFF);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "MCP23017 initialized: Port A = 8 outputs (relays), Port B = 8 inputs (opto-couplers)");
    return ESP_OK;
}
