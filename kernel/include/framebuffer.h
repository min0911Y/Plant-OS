#ifndef PLANT_FRAMEBUFFER_H
#define PLANT_FRAMEBUFFER_H
#include <ctypes.h>
typedef struct {
  uintptr_t address;
  uint32_t width, height, pitch, bpp;
  uint8_t red_shift, green_shift, blue_shift, reserved;
} framebuffer_info_t;
#endif
