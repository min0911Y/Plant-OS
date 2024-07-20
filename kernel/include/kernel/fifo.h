#pragma once
#include <define.h>
#include <type.h>
struct FIFO8 {
  u8 *buf;
  int p, q, size, free, flags;
};