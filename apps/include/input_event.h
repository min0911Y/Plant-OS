#ifndef PLANT_OS_INPUT_EVENT_H
#define PLANT_OS_INPUT_EVENT_H
#include <ctypes.h>
typedef struct {
  int32_t x, y, wheel;
  uint32_t buttons;
} mouse_event_t;
#ifdef __cplusplus
static_assert(sizeof(mouse_event_t) == 16, "mouse event ABI");
#else
_Static_assert(sizeof(mouse_event_t) == 16, "mouse event ABI");
#endif
#endif
