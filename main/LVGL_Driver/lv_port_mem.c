/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file lv_port_mem.c
 * @brief Custom memory allocation for LVGL using ESP32 PSRAM
 */

#include "lv_port_mem.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "LV_MEM";

/**
 * @brief Custom malloc implementation for LVGL using PSRAM
 * This function will attempt to allocate from PSRAM first, and fall back to internal RAM if PSRAM is not available
 */
void *lv_port_malloc(size_t size)
{
    void *ptr = NULL;
    
    // Try to allocate from PSRAM first
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (ptr == NULL) {
        // If PSRAM allocation fails, fall back to internal RAM
        ESP_LOGW(TAG, "PSRAM allocation failed for %d bytes, using internal RAM", size);
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return ptr;
}

/**
 * @brief Custom free implementation for LVGL
 */
void lv_port_free(void *ptr)
{
    if (ptr != NULL) {
        heap_caps_free(ptr);
    }
}

/**
 * @brief Custom realloc implementation for LVGL using PSRAM
 */
void *lv_port_realloc(void *ptr, size_t new_size)
{
    void *new_ptr = NULL;
    
    if (new_size == 0) {
        lv_port_free(ptr);
        return NULL;
    }
    
    if (ptr == NULL) {
        return lv_port_malloc(new_size);
    }
    
    // Try to reallocate in PSRAM first
    new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (new_ptr == NULL) {
        // If PSRAM reallocation fails, try internal RAM
        ESP_LOGW(TAG, "PSRAM reallocation failed for %d bytes, using internal RAM", new_size);
        new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return new_ptr;
}

/**
 * @brief Custom calloc implementation for LVGL using PSRAM
 */
void *lv_port_calloc(size_t num, size_t size)
{
    size_t total_size = num * size;
    void *ptr = lv_port_malloc(total_size);
    
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}
