#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

#include <ctypes.h>

typedef struct {
  uintptr_t framebuffer;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint64_t physical_address;
  uint8_t red_size, red_shift;
  uint8_t green_size, green_shift;
  uint8_t blue_size, blue_shift;
} platform_video_info_t;

bool platform_rtc_timestamp(uint32_t *timestamp);
void platform_early_initialize(void);
void *platform_text_vram(void);
bool platform_is_text_vram(const void *vram);
void *platform_graphics_vram(void);
void platform_text_cursor_set(uint16_t position);
void platform_palette_set(uint16_t start, uint16_t count, const uint8_t *rgb);
void platform_palette_reset(void);
void platform_video_legacy_text_mode(void);
void platform_video_legacy_graphics_mode(void);
int platform_video_switch_mode(int mode);
int platform_video_check_mode(int mode);
bool platform_video_set_mode(uint32_t width, uint32_t height, uint32_t bpp,
                             platform_video_info_t *info);
bool platform_video_current_info(platform_video_info_t *info);
void platform_video_text_mode(void);
void platform_video_graphics_mode(void);
bool platform_video_console_visible(void);
struct mtask;
void platform_video_release_owner(struct mtask *task);

#endif
