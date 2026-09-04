#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include <ctypes.h>

enum {
  MOUSE_ROLL_NONE = 0,
  MOUSE_ROLL_UP = 1,
  MOUSE_ROLL_DOWN = 2,
};

typedef struct mouse_decoder {
  uint8_t packet[4];
  uint8_t phase;
  int x;
  int y;
  int buttons;
  int sleeping;
  int wheel;
} mouse_decoder_t;

#endif
