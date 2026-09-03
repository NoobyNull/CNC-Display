#pragma once
// src/lv_conf.h routes LVGL's allocator at PSRAM through heap_caps_*. Mapping
// those three calls to plain malloc here means the simulator uses the project's
// real lv_conf.h rather than a forked copy that would quietly drift out of
// sync with the firmware's LVGL configuration.

#include <stdlib.h>

#define MALLOC_CAP_8BIT   (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DMA    (1 << 3)

static inline void* heap_caps_malloc(size_t sz, uint32_t) { return malloc(sz); }
static inline void  heap_caps_free(void* p) { free(p); }
static inline void* heap_caps_realloc(void* p, size_t sz, uint32_t) { return realloc(p, sz); }
static inline size_t heap_caps_get_free_size(uint32_t) { return 0; }
