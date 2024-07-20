#pragma once
#include "config.h"
#include "task.h"
#include <define.h>
#include <type.h>

struct TIMER {
  struct TIMER *next;
  u32           timeout, flags;
  struct FIFO8 *fifo;
  u8            data;
  mtask        *waiter;
};

struct TIMERCTL {
  u32           count, next;
  struct TIMER *t0;
  struct TIMER  timers0[MAX_TIMER];
};

#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040
#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040

extern struct TIMERCTL timerctl;
