#pragma once
#include <define.h>
#include <type.h>
#define MOUSE_ROLL_NONE 0
#define MOUSE_ROLL_UP   1
#define MOUSE_ROLL_DOWN 2
struct MOUSE_DEC {
  u8   buf[4], phase;
  int  x, y, btn;
  int  sleep;
  char roll;
};
extern struct MOUSE_DEC mdec;
extern int              gmx, gmy;