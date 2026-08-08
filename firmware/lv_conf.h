#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// Keep the UI framebuffer compatible with the ST7796 RGB565 path.
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// Keep LVGL's allocator bounded for the first UI milestone.
#define LV_MEM_SIZE (64U * 1024U)

#endif  // LV_CONF_H
