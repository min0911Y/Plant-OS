#ifndef KERNEL_MATH_UTIL_H
#define KERNEL_MATH_UTIL_H

#include <stddef.h>

static inline size_t size_div_round_up(size_t value, size_t divisor) {
  return value / divisor + (value % divisor != 0);
}

#endif
