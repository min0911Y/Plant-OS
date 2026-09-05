#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include <ctypes.h>

enum {
  MOUSE_ROLL_NONE = 0,
  MOUSE_ROLL_UP = 1,
  MOUSE_ROLL_DOWN = 2,
};

typedef struct {
  int32_t x, y, wheel;
  uint32_t buttons;
} mouse_event_t;

#endif
