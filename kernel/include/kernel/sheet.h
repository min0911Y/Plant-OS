#pragma once
#include "config.h"
#include "screen.h"
#include "task.h"
#include <define.h>
#include <type.h>

struct SHEET {
  vram_t        *buf;
  int            bxsize, bysize, vx0, vy0, col_inv, height, flags;
  struct SHTCTL *ctl;
  struct TASK   *task;
  void (*Close)(); // 为NULL表示没有关闭函数
  void *args;
};
struct SHTCTL {
  vram_t       *vram;
  u8           *map;
  int           xsize, ysize, top;
  struct SHEET *sheets[MAX_SHEETS];
  struct SHEET  sheets0[MAX_SHEETS];
};