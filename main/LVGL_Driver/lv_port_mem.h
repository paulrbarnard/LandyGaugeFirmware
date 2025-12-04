/**
 * @file lv_port_mem.h
 * @brief Custom memory allocation for LVGL using ESP32 PSRAM
 */

#ifndef LV_PORT_MEM_H
#define LV_PORT_MEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Custom malloc implementation for LVGL using PSRAM
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory or NULL on failure
 */
void *lv_port_malloc(size_t size);

/**
 * @brief Custom free implementation for LVGL
 * @param ptr Pointer to memory to free
 */
void lv_port_free(void *ptr);

/**
 * @brief Custom realloc implementation for LVGL using PSRAM
 * @param ptr Pointer to existing memory
 * @param new_size New size in bytes
 * @return Pointer to reallocated memory or NULL on failure
 */
void *lv_port_realloc(void *ptr, size_t new_size);

/**
 * @brief Custom calloc implementation for LVGL using PSRAM
 * @param num Number of elements
 * @param size Size of each element
 * @return Pointer to allocated and zeroed memory or NULL on failure
 */
void *lv_port_calloc(size_t num, size_t size);

// Override LVGL's default memory allocation macros
// These will be used by LVGL when LV_MEM_CUSTOM is enabled
#ifndef LV_MEM_CUSTOM_ALLOC
#define LV_MEM_CUSTOM_ALLOC   lv_port_malloc
#endif
#ifndef LV_MEM_CUSTOM_FREE
#define LV_MEM_CUSTOM_FREE    lv_port_free
#endif
#ifndef LV_MEM_CUSTOM_REALLOC
#define LV_MEM_CUSTOM_REALLOC lv_port_realloc
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_PORT_MEM_H */
