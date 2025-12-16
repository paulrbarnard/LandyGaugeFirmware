/**
 * @file warning_beep.c
 * @brief Warning beep module implementation using I2S tone generation and MP3 playback
 */

#include "warning_beep.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "PCM5101.h"
#include <math.h>
#include <dirent.h>

static const char *TAG = "WARNING_BEEP";

// Tone generation parameters
#define SAMPLE_RATE     22050
#define TONE_AMPLITUDE  8000    // Volume level

// Melodic two-tone chime frequencies (musical interval - perfect fourth)
#define TONE_FREQ_1     880     // A5 note
#define TONE_FREQ_2     1175    // D6 note (perfect fourth above A5)

// Warning repeat interval
#define WARNING_REPEAT_INTERVAL_MS  10000  // 10 seconds for roll warnings
#define PITCH_YELLOW_INTERVAL_MS    3000   // 3 seconds for pitch yellow
#define PITCH_RED_INTERVAL_MS       500    // 0.5 seconds for pitch red

// I2S handle (shared with audio player - we'll use direct write)
extern i2s_chan_handle_t i2s_tx_chan;

// Roll warning state (beep + MP3)
static warning_level_type_t current_warning_level = WARNING_LEVEL_NONE;
static warning_level_type_t pending_warning_level = WARNING_LEVEL_NONE;  // Next level after MP3 finishes
static bool warning_active = false;
static bool warning_task_running = false;
static bool mp3_playing = false;  // Track if MP3 is currently playing
static bool pending_stop = false; // Track if we should stop after MP3 finishes
static TaskHandle_t warning_task_handle = NULL;

// Pitch warning state (beeps only)
static warning_level_type_t current_pitch_level = WARNING_LEVEL_NONE;
static bool pitch_active = false;
static bool pitch_task_running = false;
static TaskHandle_t pitch_task_handle = NULL;

// Pre-generated melodic tone buffer (two-tone chime with envelope)
#define CHIME_DURATION_MS  200
#define CHIME_SAMPLES      ((SAMPLE_RATE * CHIME_DURATION_MS) / 1000)
static int16_t chime_buffer[CHIME_SAMPLES];

/**
 * @brief Generate melodic chime with envelope shaping
 * Creates a soft two-tone sound with fade in/out
 */
static void generate_tone_buffer(void)
{
    for (int i = 0; i < CHIME_SAMPLES; i++) {
        float t = (float)i / SAMPLE_RATE;
        float progress = (float)i / CHIME_SAMPLES;
        
        // Envelope: quick attack, smooth decay (more musical)
        float envelope;
        if (progress < 0.1f) {
            // Quick attack (first 10%)
            envelope = progress / 0.1f;
        } else {
            // Smooth exponential decay
            envelope = expf(-3.0f * (progress - 0.1f));
        }
        
        // Two-tone harmony (fundamental + upper note)
        float tone1 = sinf(2.0f * M_PI * TONE_FREQ_1 * t);
        float tone2 = 0.6f * sinf(2.0f * M_PI * TONE_FREQ_2 * t);  // Upper tone slightly quieter
        
        // Mix tones with envelope
        float sample = envelope * (tone1 + tone2) * 0.5f;
        
        chime_buffer[i] = (int16_t)(TONE_AMPLITUDE * sample);
    }
}

/**
 * @brief Play a melodic chime (blocking)
 * Plays the pre-generated chime buffer once for a pleasant alert sound
 */
static void play_tone(uint32_t duration_ms)
{
    (void)duration_ms;  // Duration is now fixed by CHIME_DURATION_MS
    
    if (i2s_tx_chan == NULL) {
        ESP_LOGW(TAG, "I2S not initialized");
        return;
    }
    
    // Check if we should still play
    if (!(warning_active || pitch_active)) {
        return;
    }
    
    size_t bytes_written;
    
    // Play the complete chime buffer once
    i2s_channel_write(i2s_tx_chan, chime_buffer, 
                      CHIME_SAMPLES * sizeof(int16_t), 
                      &bytes_written, pdMS_TO_TICKS(200));
}

/**
 * @brief Task to handle warning audio playback (beep + MP3) with 10s repeat
 */
static void warning_task(void *pvParameters)
{
    warning_task_running = true;
    
    while (warning_active) {
        // Play a short beep first
        ESP_LOGI(TAG, "Playing warning beep");
        play_tone(BEEP_SHORT);
        
        // Small delay between beep and MP3
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (!warning_active) break;
        
        // Mark MP3 as playing
        mp3_playing = true;
        
        // Play the appropriate MP3 based on warning level
        if (current_warning_level == WARNING_LEVEL_RED) {
            ESP_LOGI(TAG, "Playing DangerRoll.mp3");
            Play_Music("/sdcard", "DANGE~24.MP3");
        } else if (current_warning_level == WARNING_LEVEL_YELLOW) {
            ESP_LOGI(TAG, "Playing WarningRoll.mp3");
            Play_Music("/sdcard", "WARNI~26.MP3");
        }
        
        // Wait for MP3 to finish playing (poll audio player state)
        // Typical MP3 duration is 1-3 seconds, check every 100ms
        for (int i = 0; i < 50 && warning_active; i++) {  // Max 5 seconds wait
            vTaskDelay(pdMS_TO_TICKS(100));
            // Could check audio_player_get_state() here if needed
        }
        
        // MP3 finished playing
        mp3_playing = false;
        
        // Check if we should stop
        if (pending_stop) {
            pending_stop = false;
            ESP_LOGI(TAG, "Stopping roll warning after MP3 completion");
            break;
        }
        
        // Check if there's a pending level change
        if (pending_warning_level != WARNING_LEVEL_NONE) {
            current_warning_level = pending_warning_level;
            pending_warning_level = WARNING_LEVEL_NONE;
            ESP_LOGI(TAG, "Switching to pending warning level");
            continue;  // Play the new level immediately
        }
        
        // If warning_active became false while playing, exit
        if (!warning_active) {
            break;
        }
        
        // Wait remaining time before repeating (10 seconds total, minus MP3 time)
        for (int i = 0; i < 50 && warning_active; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));  // 100ms x 50 = 5 more seconds
            
            // Check for pending level change during wait
            if (pending_warning_level != WARNING_LEVEL_NONE) {
                current_warning_level = pending_warning_level;
                pending_warning_level = WARNING_LEVEL_NONE;
                ESP_LOGI(TAG, "Switching to pending warning level during wait");
                break;
            }
        }
    }
    
    mp3_playing = false;
    warning_task_running = false;
    warning_task_handle = NULL;
    vTaskDelete(NULL);
}

void warning_beep_init(void)
{
    ESP_LOGI(TAG, "Initializing warning beep system");
    
    // Generate the tone buffer
    generate_tone_buffer();
    
    // List files in /sdcard for debugging
    DIR *dir = opendir("/sdcard");
    if (dir) {
        ESP_LOGI(TAG, "Files on SD card:");
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  - %s", entry->d_name);
        }
        closedir(dir);
    } else {
        ESP_LOGE(TAG, "Failed to open /sdcard directory");
    }
    
    ESP_LOGI(TAG, "Warning beep system initialized (I2S tone + MP3 playback)");
}

void warning_beep_play(beep_duration_t duration)
{
    // Play a single beep (blocking)
    play_tone(duration);
}

void warning_beep_start(warning_level_type_t level)
{
    // If same level is already active, don't restart
    if (warning_active && current_warning_level == level) {
        return;
    }
    
    // If MP3 is currently playing, queue the level change
    if (mp3_playing && warning_active) {
        if (level == WARNING_LEVEL_NONE) {
            // Request stop after MP3 finishes
            pending_stop = true;
            pending_warning_level = WARNING_LEVEL_NONE;
            ESP_LOGI(TAG, "MP3 playing, will stop after completion");
        } else {
            // Queue the new warning level
            pending_stop = false;
            pending_warning_level = level;
            ESP_LOGI(TAG, "MP3 playing, queuing level change to %s", 
                     level == WARNING_LEVEL_RED ? "DANGER" : "WARNING");
        }
        return;
    }
    
    // Stop any existing warning first (if not playing MP3)
    warning_beep_stop();
    
    if (level == WARNING_LEVEL_NONE) {
        ESP_LOGI(TAG, "No warning to start");
        return;
    }
    
    // Configure new warning
    current_warning_level = level;
    pending_warning_level = WARNING_LEVEL_NONE;
    pending_stop = false;
    warning_active = true;
    
    // Create task to handle warning audio
    xTaskCreatePinnedToCore(
        warning_task,
        "WarningTask",
        4096,  // Larger stack for MP3 playback
        NULL,
        2,  // Lower priority than LVGL
        &warning_task_handle,
        1   // Run on core 1 (audio core)
    );
    
    ESP_LOGI(TAG, "Started warning audio: level=%s", 
             level == WARNING_LEVEL_RED ? "DANGER" : "WARNING");
}

void warning_beep_stop(void)
{
    bool had_warning = warning_active || warning_task_handle != NULL;
    bool had_pitch = pitch_active || pitch_task_handle != NULL;
    
    if (!had_warning && !had_pitch) {
        return;  // Nothing to stop
    }
    
    ESP_LOGI(TAG, "Stopping warning audio...");
    
    // Stop roll warnings
    warning_active = false;
    current_warning_level = WARNING_LEVEL_NONE;
    
    // Stop pitch warnings
    pitch_active = false;
    current_pitch_level = WARNING_LEVEL_NONE;
    
    // Stop any playing music
    Music_pause();
    
    // Force delete the roll warning task if it's still running
    if (warning_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (warning_task_running) {
            vTaskDelete(warning_task_handle);
            warning_task_running = false;
            ESP_LOGI(TAG, "Roll warning task forcefully deleted");
        }
        warning_task_handle = NULL;
    }
    
    // Force delete the pitch warning task if it's still running
    if (pitch_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (pitch_task_running) {
            vTaskDelete(pitch_task_handle);
            pitch_task_running = false;
            ESP_LOGI(TAG, "Pitch warning task forcefully deleted");
        }
        pitch_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Stopped all warning audio");
}

/**
 * @brief Task to handle pitch warning beeps (beeps only, no MP3)
 */
static void pitch_warning_task(void *pvParameters)
{
    pitch_task_running = true;
    
    uint32_t interval_ms = (current_pitch_level == WARNING_LEVEL_RED) ? 
                           PITCH_RED_INTERVAL_MS : PITCH_YELLOW_INTERVAL_MS;
    
    while (pitch_active) {
        // Play a short beep
        play_tone(BEEP_SHORT);
        
        // Wait for the interval (check periodically to allow early exit)
        uint32_t waited = 0;
        while (waited < interval_ms && pitch_active) {
            uint32_t delay = (interval_ms - waited > 50) ? 50 : (interval_ms - waited);
            vTaskDelay(pdMS_TO_TICKS(delay));
            waited += delay;
        }
    }
    
    pitch_task_running = false;
    pitch_task_handle = NULL;
    vTaskDelete(NULL);
}

void warning_pitch_start(warning_level_type_t level)
{
    // If same level is already active, don't restart
    if (pitch_active && current_pitch_level == level) {
        return;
    }
    
    // Stop existing pitch warnings (but not roll warnings)
    pitch_active = false;
    if (pitch_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (pitch_task_running) {
            vTaskDelete(pitch_task_handle);
            pitch_task_running = false;
        }
        pitch_task_handle = NULL;
    }
    
    if (level == WARNING_LEVEL_NONE) {
        current_pitch_level = WARNING_LEVEL_NONE;
        ESP_LOGI(TAG, "Pitch warning stopped");
        return;
    }
    
    // Configure new pitch warning
    current_pitch_level = level;
    pitch_active = true;
    
    // Create task to handle pitch beeps
    xTaskCreatePinnedToCore(
        pitch_warning_task,
        "PitchBeepTask",
        4096,
        NULL,
        2,
        &pitch_task_handle,
        1
    );
    
    ESP_LOGI(TAG, "Started pitch beeps: level=%s, interval=%ldms", 
             level == WARNING_LEVEL_RED ? "RED" : "YELLOW",
             (long)(level == WARNING_LEVEL_RED ? PITCH_RED_INTERVAL_MS : PITCH_YELLOW_INTERVAL_MS));
}
