#ifndef KERNEL_X86_CLOCK_H
#define KERNEL_X86_CLOCK_H

#include <ctypes.h>

static inline uint64_t x86_tsc_read(void) {
  uint32_t low, high;
  asm volatile("rdtsc" : "=a"(low), "=d"(high) : : "memory");
  return ((uint64_t)high << 32) | low;
}

uint64_t x86_tsc_frequency_khz(void);

#endif
