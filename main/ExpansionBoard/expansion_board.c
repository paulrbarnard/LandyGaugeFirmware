/**
 * @file expansion_board.c
 * @brief Expansion board manager implementation
 *
 * Manages the MCP23017 I/O expander, ADS1115 ADC, and LIS3MDL magnetometer.
 * Runs a FreeRTOS task that polls digital inputs at ~20 Hz with software
 * debouncing, and provides thread-safe access to all peripherals.
 */

#include "expansion_board.h"
#include "mcp23017.h"
#include "ads1115.h"
#include "lis3mdl.h"
#include "mcp9600.h"
#include "I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "EXBD";

/*******************************************************************************
 * Configuration
 ******************************************************************************/
#define EXBD_POLL_INTERVAL_MS   50      // Input polling interval (20 Hz)
#define EXBD_DEBOUNCE_MS        50      // Debounce time for inputs
#define EXBD_TASK_STACK_SIZE    3072    // Polling task stack
#define EXBD_TASK_PRIORITY      2       // Lower than Driver_Loop (3)

/*******************************************************************************
 * Internal state
 ******************************************************************************/

// Detection flags
static bool board_detected = false;
static bool mcp23017_ok = false;
static bool ads1115_ok = false;
static bool qmc5883l_ok = false;  // renamed internally but actually LIS3MDL
static bool mcp9600_ok = false;   // MCP9600 thermocouple converter

// Digital input state
static exbd_inputs_snapshot_t input_snapshot;
static SemaphoreHandle_t input_mutex = NULL;

// Debounce state
typedef struct {
    uint8_t stable_state;       // Last known stable (debounced) state byte
    uint8_t raw_state;          // Last raw reading  
    uint32_t last_change_time;  // When the raw reading last changed (tick count)
} debounce_state_t;
static debounce_state_t debounce = { 0 };

// Select button edge detection
static volatile bool select_press_pending = false;

// User callback
static exbd_input_callback_t user_callback = NULL;

// Polling task handle
static TaskHandle_t poll_task_handle = NULL;

/*******************************************************************************
 * Input name lookup
 ******************************************************************************/

static const char *input_names[EXBD_INPUT_COUNT] = {
    [EXBD_INPUT_SELECT]     = "Select",
    [EXBD_INPUT_IGNITION]   = "Ignition",
    [EXBD_INPUT_LIGHTS]     = "Lights",
    [EXBD_INPUT_FAN_LOW]    = "Fan Low",
    [EXBD_INPUT_FAN_HIGH]   = "Fan High",
    [EXBD_INPUT_SPARE_5]    = "Spare 5",
    [EXBD_INPUT_COOLANT_LO] = "Coolant Low",
    [EXBD_INPUT_SPARE_7]    = "Spare 7",
};

const char *exbd_input_name(exbd_input_t input)
{
    if (input >= EXBD_INPUT_COUNT) return "Unknown";
    return input_names[input];
}

/*******************************************************************************
 * Fast I2C probe (50ms timeout instead of 1000ms)
 ******************************************************************************/
static bool i2c_probe(uint8_t addr)
{
    // Just send START + addr + STOP with a short timeout
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
}

/*******************************************************************************
 * Debounced input polling
 ******************************************************************************/

/**
 * @brief Process a new raw reading and update debounced state
 * @param raw_byte Raw 8-bit reading from MCP23017 Port A
 * @param now_ms Current tick time in ms
 */
static void process_inputs(uint8_t raw_byte, uint32_t now_ms)
{
    // If raw reading changed from last time, reset the debounce timer
    if (raw_byte != debounce.raw_state) {
        debounce.raw_state = raw_byte;
        debounce.last_change_time = now_ms;
        return;  // Wait for stable period
    }

    // If raw has been stable for DEBOUNCE_MS, accept as new state
    if ((now_ms - debounce.last_change_time) < EXBD_DEBOUNCE_MS) {
        return;  // Still within debounce window
    }

    // Check what changed from the last stable state
    uint8_t changed = debounce.stable_state ^ raw_byte;
    if (changed == 0) return;  // No change

    uint8_t old_stable = debounce.stable_state;
    debounce.stable_state = raw_byte;

    // Update snapshot and fire callbacks
    if (xSemaphoreTake(input_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        input_snapshot.raw_byte = raw_byte;

        for (int i = 0; i < EXBD_INPUT_COUNT; i++) {
            bool new_state = (raw_byte >> i) & 0x01;
            bool old_state = (old_stable >> i) & 0x01;

            input_snapshot.inputs[i].current = new_state;

            if (((changed >> i) & 0x01)) {
                input_snapshot.inputs[i].changed = true;

                if (new_state) {
                    input_snapshot.inputs[i].on_time = now_ms;
                }

                ESP_LOGD(TAG, "Input %s (%d): %s → %s",
                         input_names[i], i,
                         old_state ? "ON" : "OFF",
                         new_state ? "ON" : "OFF");

                // Edge detection for select button
                if (i == EXBD_INPUT_SELECT && new_state && !old_state) {
                    select_press_pending = true;
                }

                // Fire callback
                if (user_callback) {
                    user_callback((exbd_input_t)i, new_state);
                }
            }
        }

        xSemaphoreGive(input_mutex);
    }
}

/*******************************************************************************
 * Polling task
 ******************************************************************************/

static void expansion_poll_task(void *param)
{
    ESP_LOGI(TAG, "Polling task started (interval=%dms)", EXBD_POLL_INTERVAL_MS);

    while (1) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Read digital inputs from MCP23017 Port B (opto-couplers)
        if (mcp23017_ok) {
            uint8_t port_b = 0;
            if (mcp23017_read_port('B', &port_b) == ESP_OK) {
                process_inputs(port_b, now_ms);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(EXBD_POLL_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

esp_err_t expansion_board_init(void)
{
    ESP_LOGI(TAG, "Initializing expansion board...");

    // Create mutex for thread-safe input access
    input_mutex = xSemaphoreCreateMutex();
    if (!input_mutex) {
        ESP_LOGE(TAG, "Failed to create input mutex");
        return ESP_ERR_NO_MEM;
    }

    // Clear state
    memset(&input_snapshot, 0, sizeof(input_snapshot));
    memset(&debounce, 0, sizeof(debounce));

    // Initialize MCP23017 (I/O expander) — fast probe first to avoid 1s timeout
    if (i2c_probe(MCP23017_I2C_ADDR) && mcp23017_init() == ESP_OK) {
        mcp23017_ok = true;
        board_detected = true;
        ESP_LOGI(TAG, "MCP23017 I/O expander: OK");
    } else {
        ESP_LOGW(TAG, "MCP23017 I/O expander: NOT FOUND");
    }

    // Initialize ADS1115 (ADC) — fast probe first
    if (i2c_probe(ADS1115_I2C_ADDR) && ads1115_init() == ESP_OK) {
        ads1115_ok = true;
        board_detected = true;
        ESP_LOGI(TAG, "ADS1115 ADC: OK");
    } else {
        ESP_LOGW(TAG, "ADS1115 ADC: NOT FOUND");
    }

    // Initialize LIS3MDL (magnetometer) — fast probe first
    if (i2c_probe(LIS3MDL_I2C_ADDR) && lis3mdl_init() == ESP_OK) {
        qmc5883l_ok = true;
        board_detected = true;
        ESP_LOGI(TAG, "LIS3MDL magnetometer: OK");
    } else {
        ESP_LOGW(TAG, "LIS3MDL magnetometer: NOT FOUND");
    }

    // Initialize MCP9600 (thermocouple converter) — fast probe first
    if (i2c_probe(MCP9600_I2C_ADDR) && mcp9600_init() == ESP_OK) {
        mcp9600_ok = true;
        board_detected = true;
        ESP_LOGI(TAG, "MCP9600 thermocouple: OK");
    } else {
        ESP_LOGW(TAG, "MCP9600 thermocouple: NOT FOUND");
    }

    if (!board_detected) {
        ESP_LOGW(TAG, "No expansion board devices detected");
        return ESP_ERR_NOT_FOUND;
    }

    // Read initial input state 
    if (mcp23017_ok) {
        uint8_t initial = 0;
        mcp23017_read_port('B', &initial);
        debounce.stable_state = initial;
        debounce.raw_state = initial;
        debounce.last_change_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (xSemaphoreTake(input_mutex, portMAX_DELAY) == pdTRUE) {
            input_snapshot.raw_byte = initial;
            for (int i = 0; i < EXBD_INPUT_COUNT; i++) {
                input_snapshot.inputs[i].current = (initial >> i) & 0x01;
                input_snapshot.inputs[i].changed = false;
            }
            xSemaphoreGive(input_mutex);
        }

        ESP_LOGI(TAG, "Initial inputs: 0x%02X (IGN=%d, LIGHTS=%d, FAN_L=%d, FAN_H=%d, COOL=%d)",
                 initial,
                 (initial >> EXBD_INPUT_IGNITION) & 1,
                 (initial >> EXBD_INPUT_LIGHTS) & 1,
                 (initial >> EXBD_INPUT_FAN_LOW) & 1,
                 (initial >> EXBD_INPUT_FAN_HIGH) & 1,
                 (initial >> EXBD_INPUT_COOLANT_LO) & 1);
    }

    // Start polling task
    BaseType_t ret = xTaskCreatePinnedToCore(
        expansion_poll_task,
        "exbd_poll",
        EXBD_TASK_STACK_SIZE,
        NULL,
        EXBD_TASK_PRIORITY,
        &poll_task_handle,
        0   // Core 0 (same as Driver_Loop)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to start polling task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Expansion board initialized (MCP23017=%s, ADS1115=%s, LIS3MDL=%s, MCP9600=%s)",
             mcp23017_ok ? "YES" : "NO",
             ads1115_ok ? "YES" : "NO",
             qmc5883l_ok ? "YES" : "NO",
             mcp9600_ok ? "YES" : "NO");

    return ESP_OK;
}

bool expansion_board_detected(void)
{
    return board_detected;
}

bool exbd_has_io(void)
{
    return mcp23017_ok;
}

/*******************************************************************************
 * Digital Inputs
 ******************************************************************************/

bool exbd_get_input(exbd_input_t input)
{
    if (input >= EXBD_INPUT_COUNT || !mcp23017_ok) return false;

    bool state = false;
    if (xSemaphoreTake(input_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        state = input_snapshot.inputs[input].current;
        xSemaphoreGive(input_mutex);
    }
    return state;
}

void exbd_get_inputs(exbd_inputs_snapshot_t *snapshot)
{
    if (!snapshot) return;

    if (xSemaphoreTake(input_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(snapshot, &input_snapshot, sizeof(exbd_inputs_snapshot_t));
        // Clear the changed flags after reading
        for (int i = 0; i < EXBD_INPUT_COUNT; i++) {
            input_snapshot.inputs[i].changed = false;
        }
        xSemaphoreGive(input_mutex);
    }
}

bool exbd_select_pressed(void)
{
    if (select_press_pending) {
        select_press_pending = false;
        return true;
    }
    return false;
}

void exbd_register_input_callback(exbd_input_callback_t callback)
{
    user_callback = callback;
}

/*******************************************************************************
 * ADC
 ******************************************************************************/

esp_err_t exbd_read_adc(exbd_adc_channel_t channel, float *voltage)
{
    if (!ads1115_ok) return ESP_ERR_INVALID_STATE;
    if (channel >= EXBD_ADC_COUNT) return ESP_ERR_INVALID_ARG;
    return ads1115_read_single((uint8_t)channel, voltage);
}

esp_err_t exbd_read_all_adc(float voltages[4])
{
    if (!ads1115_ok) return ESP_ERR_INVALID_STATE;

    for (int i = 0; i < 4; i++) {
        esp_err_t ret = ads1115_read_single(i, &voltages[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC channel %d read failed", i);
            return ret;
        }
    }
    return ESP_OK;
}

/*******************************************************************************
 * Magnetometer
 ******************************************************************************/

esp_err_t exbd_get_heading(float *heading)
{
    if (!qmc5883l_ok) return ESP_ERR_INVALID_STATE;
    return lis3mdl_get_heading(heading);
}

esp_err_t exbd_get_magnetic_field(float *x, float *y, float *z)
{
    if (!qmc5883l_ok) return ESP_ERR_INVALID_STATE;

    lis3mdl_data_t data;
    esp_err_t ret = lis3mdl_read_data(&data);
    if (ret != ESP_OK) return ret;

    if (x) *x = data.x;
    if (y) *y = data.y;
    if (z) *z = data.z;
    return ESP_OK;
}
