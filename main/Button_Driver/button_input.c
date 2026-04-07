/**
 * @file button_input.c
 * @brief Debounced GPIO button input driver implementation
 * 
 * Uses polling with software debounce for reliable button press detection.
 * All buttons are active low (pull to GND when pressed).
 * Polling is appropriate since main loop already runs every 10ms.
 *
 * Buttons:
 *   GPIO0  - Boot button (next gauge, legacy)
 *   GPIO43 - Next gauge button
 *   GPIO44 - Previous gauge button
 */

#include "button_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "button_input";

// Configuration
#define BUTTON_BOOT_GPIO    GPIO_NUM_0      // Boot button (next)
#define BUTTON_NEXT_GPIO    GPIO_NUM_43     // Next gauge button
#define BUTTON_PREV_GPIO    GPIO_NUM_44     // Previous gauge button
#define DEBOUNCE_TIME_MS    50              // Debounce time in milliseconds
#define BUTTON_ACTIVE_LEVEL 0               // Active low (pressed = 0)

// Per-button debounce state
typedef struct {
    gpio_num_t gpio;
    bool last_state;            // false = not pressed
    bool press_pending;         // Edge-triggered pending flag
    int64_t last_change_time;   // Timestamp of last state change (µs)
} button_state_t;

static bool initialized = false;

// Button instances
static button_state_t btn_boot = { .gpio = BUTTON_BOOT_GPIO };
static button_state_t btn_next = { .gpio = BUTTON_NEXT_GPIO };
static button_state_t btn_prev = { .gpio = BUTTON_PREV_GPIO };

/**
 * @brief Poll a single button with debounce (internal helper)
 * @return true if a new press was detected this call
 */
static bool poll_button(button_state_t *btn)
{
    bool current_state = (gpio_get_level(btn->gpio) == BUTTON_ACTIVE_LEVEL);
    int64_t now = esp_timer_get_time();

    if (current_state != btn->last_state) {
        // State changed — accept only if debounce period has elapsed
        if ((now - btn->last_change_time) > (DEBOUNCE_TIME_MS * 1000)) {
            btn->last_state = current_state;
            btn->last_change_time = now;

            // Rising edge (just pressed)
            if (current_state) {
                return true;
            }
        }
    } else {
        // State stable — keep the timestamp current
        btn->last_change_time = now;
    }
    return false;
}

static void init_button_state(button_state_t *btn)
{
    btn->last_state = (gpio_get_level(btn->gpio) == BUTTON_ACTIVE_LEVEL);
    btn->last_change_time = esp_timer_get_time();
    btn->press_pending = false;
}

esp_err_t button_input_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Button input already initialized");
        return ESP_OK;
    }

    // Configure all three buttons: input, pull-up, no interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_BOOT_GPIO) |
                        (1ULL << BUTTON_NEXT_GPIO) |
                        (1ULL << BUTTON_PREV_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIOs: %s", esp_err_to_name(ret));
        return ret;
    }

    init_button_state(&btn_boot);
    init_button_state(&btn_next);
    init_button_state(&btn_prev);

    initialized = true;
    ESP_LOGI(TAG, "Button input initialized: BOOT=GPIO%d, NEXT=GPIO%d, PREV=GPIO%d (active low, %dms debounce)",
             BUTTON_BOOT_GPIO, BUTTON_NEXT_GPIO, BUTTON_PREV_GPIO, DEBOUNCE_TIME_MS);

    return ESP_OK;
}

bool button_input_pressed(void)
{
    if (!initialized) return false;

    // Check pending flags first (boot or next)
    if (btn_boot.press_pending) {
        btn_boot.press_pending = false;
        return true;
    }
    if (btn_next.press_pending) {
        btn_next.press_pending = false;
        return true;
    }

    // Poll both "next" buttons
    if (poll_button(&btn_boot)) return true;
    if (poll_button(&btn_next)) return true;

    return false;
}

bool button_input_prev_pressed(void)
{
    if (!initialized) return false;

    if (btn_prev.press_pending) {
        btn_prev.press_pending = false;
        return true;
    }

    return poll_button(&btn_prev);
}

bool button_input_both_held(void)
{
    if (!initialized) return false;
    return button_input_next_held() && button_input_prev_held();
}

bool button_input_next_held(void)
{
    if (!initialized) return false;
    return (gpio_get_level(BUTTON_NEXT_GPIO) == BUTTON_ACTIVE_LEVEL) ||
           (gpio_get_level(BUTTON_BOOT_GPIO) == BUTTON_ACTIVE_LEVEL);
}

bool button_input_prev_held(void)
{
    if (!initialized) return false;
    return (gpio_get_level(BUTTON_PREV_GPIO) == BUTTON_ACTIVE_LEVEL);
}

void button_input_cleanup(void)
{
    if (!initialized) return;

    gpio_reset_pin(BUTTON_BOOT_GPIO);
    gpio_reset_pin(BUTTON_NEXT_GPIO);
    gpio_reset_pin(BUTTON_PREV_GPIO);

    initialized = false;
    ESP_LOGI(TAG, "Button input cleanup complete");
}
