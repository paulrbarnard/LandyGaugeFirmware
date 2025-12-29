/**
 * @file button_input.c
 * @brief Debounced GPIO button input driver implementation
 * 
 * Uses polling with software debounce for reliable
 * button press detection. Active low (button pulls to GND when pressed).
 * Polling is appropriate since main loop already runs every 10ms.
 */

#include "button_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "button_input";

// Configuration
#define BUTTON_GPIO         GPIO_NUM_0      // Boot button, pulls low when pressed
#define DEBOUNCE_TIME_MS    50              // Debounce time in milliseconds
#define BUTTON_ACTIVE_LEVEL 0               // Active low (pressed = 0)

// State
static bool initialized = false;
static bool last_button_state = false;      // false = not pressed
static bool button_pressed_pending = false;
static int64_t last_state_change_time = 0;

esp_err_t button_input_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Button input already initialized");
        return ESP_OK;
    }
    
    // Configure GPIO0 as input with internal pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,     // No interrupt, we'll poll
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", BUTTON_GPIO, esp_err_to_name(ret));
        return ret;
    }
    
    initialized = true;
    last_button_state = (gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE_LEVEL);
    last_state_change_time = esp_timer_get_time();
    button_pressed_pending = false;
    
    ESP_LOGI(TAG, "Button input initialized on GPIO%d (active low, %dms debounce, polling mode)", 
             BUTTON_GPIO, DEBOUNCE_TIME_MS);
    
    return ESP_OK;
}

bool button_input_pressed(void)
{
    if (!initialized) {
        return false;
    }
    
    // If we have a pending press, return it and clear
    if (button_pressed_pending) {
        button_pressed_pending = false;
        return true;
    }
    
    // Read current button state
    bool current_state = (gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE_LEVEL);
    int64_t now = esp_timer_get_time();
    
    // Check for state change with debounce
    if (current_state != last_button_state) {
        // State changed, check if debounce time has passed
        if ((now - last_state_change_time) > (DEBOUNCE_TIME_MS * 1000)) {
            last_button_state = current_state;
            last_state_change_time = now;
            
            // If button is now pressed (transitioned to pressed state)
            if (current_state) {
                return true;
            }
        }
    } else {
        // State stable, update the timestamp
        last_state_change_time = now;
    }
    
    return false;
}

void button_input_cleanup(void)
{
    if (!initialized) {
        return;
    }
    
    // Reset GPIO to default state
    gpio_reset_pin(BUTTON_GPIO);
    
    initialized = false;
    ESP_LOGI(TAG, "Button input cleanup complete");
}
