/**
 * @file lv_mem_custom.c
 * @brief Custom LVGL memory allocation using ESP32 PSRAM
 * 
 * This file provides custom memory allocation functions for LVGL 9.x
 * when LV_USE_STDLIB_MALLOC is set to LV_STDLIB_CUSTOM.
 * Allocates memory from ESP32's PSRAM (SPIRAM).
 */

#include "lv_conf.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <esp_heap_caps.h>
#include <stddef.h>
#include "lvgl.h"

/**
 * Initialize memory management (nothing to do for PSRAM)
 */
void lv_mem_init(void)
{
    /* PSRAM is already initialized by ESP-IDF */
}

/**
 * Deinitialize memory management (nothing to do for PSRAM)
 */
void lv_mem_deinit(void)
{
    /* Nothing to deinit */
}

/**
 * Add a memory pool (not supported for PSRAM)
 */
lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

/**
 * Remove a memory pool (not supported for PSRAM)
 */
void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

/**
 * Allocate memory from PSRAM
 * @param size The size to allocate in bytes
 * @return Pointer to allocated memory or NULL on failure
 */
void * lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

/**
 * Reallocate memory in PSRAM
 * @param p Pointer to memory to reallocate
 * @param new_size New size in bytes
 * @return Pointer to reallocated memory or NULL on failure
 */
void * lv_realloc_core(void * p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM);
}

/**
 * Free memory from PSRAM
 * @param p Pointer to memory to free
 */
void lv_free_core(void * p)
{
    heap_caps_free(p);
}

/**
 * Memory monitor (provides PSRAM stats)
 * @param mon_p Pointer to monitor structure to fill
 */
void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    if(mon_p == NULL) return;
    
    size_t free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    
    mon_p->total_size = total_size;
    mon_p->free_size = free_size;
    mon_p->free_biggest_size = largest_block;
    mon_p->used_cnt = 0;  /* Not available */
    mon_p->free_cnt = 0;  /* Not available */
    
    if(total_size > 0) {
        mon_p->used_pct = (uint8_t)(((total_size - free_size) * 100) / total_size);
        mon_p->frag_pct = (uint8_t)(((free_size - largest_block) * 100) / free_size);
    } else {
        mon_p->used_pct = 0;
        mon_p->frag_pct = 0;
    }
}

/**
 * Test memory integrity (always OK for PSRAM)
 * @return LV_RESULT_OK always
 */
lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

#endif /* LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM */
