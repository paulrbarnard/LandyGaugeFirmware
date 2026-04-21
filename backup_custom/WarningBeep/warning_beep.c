/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file warning_beep.c
 * @brief Warning beep module implementation
 */

#include "warning_beep.h"
#include "Buzzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "WARNING_BEEP";

// Repeating beep state
static TimerHandle_t beep_timer = NULL;
static TimerHandle_t beep_off_timer = NULL;
static beep_duration_t current_beep_duration = BEEP_SHORT;
static bool repeat_enabled = false;

/**
 * @brief Timer callback to turn off beep
 */
static void beep_off_timer_callback(TimerHandle_t xTimer)
{
    Buzzer_Off();  // Turn off buzzer
}

/**
 * @brief Timer callback for repeating beeps
 */
static void beep_timer_callback(TimerHandle_t xTimer)
{
    if (repeat_enabled) {
        // Start beep
        Buzzer_On();
        // Schedule turn off
        xTimerChangePeriod(beep_off_timer, pdMS_TO_TICKS(current_beep_duration), 0);
        xTimerStart(beep_off_timer, 0);
    }
}

void warning_beep_init(void)
{
    ESP_LOGI(TAG, "Initializing warning beep system");
    
    // Buzzer should already be initialized by main.c
    
    // Create timer for turning off beeps
    beep_off_timer = xTimerCreate(
        "BeepOffTimer",
        pdMS_TO_TICKS(100),   // Default duration
        pdFALSE,              // One-shot
        NULL,                 // Timer ID
        beep_off_timer_callback
    );
    
    if (beep_off_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create beep off timer");
    }
    
    // Create timer for repeating beeps
    beep_timer = xTimerCreate(
        "BeepTimer",
        pdMS_TO_TICKS(1000),  // Default 1 second (will be changed)
        pdTRUE,               // Auto-reload
        NULL,                 // Timer ID
        beep_timer_callback   // Callback function
    );
    
    if (beep_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create beep timer");
    }
}

void warning_beep_play(beep_duration_t duration)
{
    // Play buzzer for specified duration
    Buzzer_On();  // Turn on buzzer
    vTaskDelay(pdMS_TO_TICKS(duration));
    Buzzer_Off();  // Turn off buzzer
}

void warning_beep_repeat(beep_duration_t duration, uint32_t interval_ms)
{
    if (beep_timer == NULL || beep_off_timer == NULL) {
        ESP_LOGE(TAG, "Beep timers not initialized");
        return;
    }
    
    // Stop any existing timers
    xTimerStop(beep_timer, 0);
    xTimerStop(beep_off_timer, 0);
    repeat_enabled = false;
    Buzzer_Off();  // Ensure buzzer is off
    
    if (interval_ms == 0) {
        // Stop repeating
        ESP_LOGI(TAG, "Stopping repeating beeps");
        return;
    }
    
    // Configure and start new repeating beep
    current_beep_duration = duration;
    repeat_enabled = true;
    
    // Play first beep immediately (non-blocking)
    Buzzer_On();
    xTimerChangePeriod(beep_off_timer, pdMS_TO_TICKS(duration), 0);
    xTimerStart(beep_off_timer, 0);
    
    // Update timer period and start
    xTimerChangePeriod(beep_timer, pdMS_TO_TICKS(interval_ms), 0);
    xTimerStart(beep_timer, 0);
    
    ESP_LOGI(TAG, "Started repeating beeps: duration=%dms, interval=%dms", 
             (int)duration, (int)interval_ms);
}

void warning_beep_stop(void)
{
    if (beep_timer != NULL) {
        xTimerStop(beep_timer, 0);
    }
    if (beep_off_timer != NULL) {
        xTimerStop(beep_off_timer, 0);
    }
    repeat_enabled = false;
    Buzzer_Off();  // Ensure buzzer is off
    ESP_LOGI(TAG, "Stopped all beeps");
}
