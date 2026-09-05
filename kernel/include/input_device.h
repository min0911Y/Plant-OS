#ifndef PLANT_OS_INPUT_DEVICE_H
#define PLANT_OS_INPUT_DEVICE_H

#include <ctypes.h>
#include <mouse.h>

struct mtask;
extern struct mtask *keyboard_use_task, *mouse_use_task;

/* Logical key codes use Set 1 numbering with bit 8 for extended keys.
 * Device decoders convert their wire protocol before entering this layer. */
typedef struct {
  uint8_t down[64];
  bool software_repeat;
} input_keyboard_t;

typedef struct {
  uint32_t buttons;
} input_pointer_t;

bool input_keyboard_event(input_keyboard_t *source, uint16_t code,
                          bool pressed);
void input_keyboard_release(input_keyboard_t *source);
void input_keyboard_tick(void);
bool input_mouse_event(input_pointer_t *source, const mouse_event_t *event);
bool input_mouse_read(mouse_event_t *event);
void mouse_ready(void);
void mouse_sleep(void);

#endif
