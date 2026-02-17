/**
 * @file mcp23017.h
 * @brief MCP23017 16-bit I2C I/O Expander driver
 *
 * Driver for the MCP23017 on the expansion board.
 * Port A (GPA0-GPA7) is used for 8 relay outputs (drives AOZ1304N MOSFETs).
 * Port B (GPB0-GPB7) is used for 8 digital inputs (EL817S1 opto-coupler outputs).
 *
 * The hardware uses EL817S1 opto-couplers as input isolation. When an input is
 * active (12V), the opto-coupler phototransistor conducts and pulls the GPB pin LOW.
 * The IPOL register is used to invert so software reads active = 1 as expected.
 *
 * I2C address: 0x21 (A0=1, A1=0, A2=0).
 */

#ifndef MCP23017_H
#define MCP23017_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*******************************************************************************
 * I2C Configuration
 ******************************************************************************/
#define MCP23017_I2C_ADDR       0x21    // A2=0, A1=0, A0=1 (no conflict with TCA9554 at 0x20)

/*******************************************************************************
 * Register Addresses (IOCON.BANK = 0, sequential/paired mode - default)
 ******************************************************************************/
#define MCP23017_REG_IODIRA     0x00    // Port A direction (1=input, 0=output)
#define MCP23017_REG_IODIRB     0x01    // Port B direction
#define MCP23017_REG_IPOLA      0x02    // Port A input polarity inversion
#define MCP23017_REG_IPOLB      0x03    // Port B input polarity inversion
#define MCP23017_REG_GPINTENA   0x04    // Port A interrupt-on-change enable
#define MCP23017_REG_GPINTENB   0x05    // Port B interrupt-on-change enable
#define MCP23017_REG_DEFVALA    0x06    // Port A default compare for interrupt
#define MCP23017_REG_DEFVALB    0x07    // Port B default compare for interrupt
#define MCP23017_REG_INTCONA    0x08    // Port A interrupt control
#define MCP23017_REG_INTCONB    0x09    // Port B interrupt control
#define MCP23017_REG_IOCON      0x0A    // Configuration register
#define MCP23017_REG_IOCONB     0x0B    // Configuration register (mirror)
#define MCP23017_REG_GPPUA      0x0C    // Port A pull-up enable
#define MCP23017_REG_GPPUB      0x0D    // Port B pull-up enable
#define MCP23017_REG_INTFA      0x0E    // Port A interrupt flag
#define MCP23017_REG_INTFB      0x0F    // Port B interrupt flag
#define MCP23017_REG_INTCAPA    0x10    // Port A interrupt capture
#define MCP23017_REG_INTCAPB    0x11    // Port B interrupt capture
#define MCP23017_REG_GPIOA      0x12    // Port A GPIO values
#define MCP23017_REG_GPIOB      0x13    // Port B GPIO values
#define MCP23017_REG_OLATA      0x14    // Port A output latch
#define MCP23017_REG_OLATB      0x15    // Port B output latch

/*******************************************************************************
 * Low-level register access
 ******************************************************************************/

/**
 * @brief Write a single byte to an MCP23017 register
 * @param reg Register address
 * @param data Byte to write
 * @return ESP_OK on success
 */
esp_err_t mcp23017_write_reg(uint8_t reg, uint8_t data);

/**
 * @brief Read a single byte from an MCP23017 register
 * @param reg Register address
 * @param data Pointer to store the read byte
 * @return ESP_OK on success
 */
esp_err_t mcp23017_read_reg(uint8_t reg, uint8_t *data);

/*******************************************************************************
 * Port configuration
 ******************************************************************************/

/**
 * @brief Set the direction of all 8 pins on a port
 * @param port 'A' or 'B'
 * @param dir_mask Bitmask: 1 = input, 0 = output
 * @return ESP_OK on success
 */
esp_err_t mcp23017_set_port_direction(char port, uint8_t dir_mask);

/**
 * @brief Enable internal pull-ups on a port
 * @param port 'A' or 'B'
 * @param pullup_mask Bitmask: 1 = pull-up enabled
 * @return ESP_OK on success
 */
esp_err_t mcp23017_set_port_pullups(char port, uint8_t pullup_mask);

/**
 * @brief Set input polarity inversion on a port
 * @param port 'A' or 'B'
 * @param ipol_mask Bitmask: 1 = inverted (GPIO reads opposite of pin)
 * @return ESP_OK on success
 */
esp_err_t mcp23017_set_port_polarity(char port, uint8_t ipol_mask);

/*******************************************************************************
 * GPIO read/write
 ******************************************************************************/

/**
 * @brief Read all 8 pins of a port
 * @param port 'A' or 'B'
 * @param value Pointer to store the 8-bit value
 * @return ESP_OK on success
 */
esp_err_t mcp23017_read_port(char port, uint8_t *value);

/**
 * @brief Read a single pin
 * @param port 'A' or 'B'
 * @param pin Pin number 0-7
 * @return Pin state (0 or 1), or -1 on error
 */
int mcp23017_read_pin(char port, uint8_t pin);

/**
 * @brief Write all 8 pins of a port (output latch)
 * @param port 'A' or 'B'
 * @param value 8-bit value to write
 * @return ESP_OK on success
 */
esp_err_t mcp23017_write_port(char port, uint8_t value);

/**
 * @brief Write a single output pin without affecting others
 * @param port 'A' or 'B'
 * @param pin Pin number 0-7
 * @param state true = high, false = low
 * @return ESP_OK on success
 */
esp_err_t mcp23017_write_pin(char port, uint8_t pin, bool state);

/*******************************************************************************
 * Initialization
 ******************************************************************************/

/**
 * @brief Initialize the MCP23017
 *
 * Configures Port A as all inputs with polarity inversion (to compensate for
 * the MOSFET inverting buffers on the expansion board). Port B defaults to
 * outputs.
 *
 * @return ESP_OK on success, or an error code
 */
esp_err_t mcp23017_init(void);

#ifdef __cplusplus
}
#endif

#endif // MCP23017_H
