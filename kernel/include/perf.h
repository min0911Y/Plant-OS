#ifndef _PERF_H
#define _PERF_H

#include <ctypes.h>

typedef struct perf_irq_frame_t {
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp_dummy;
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;
  uint32_t gs;
  uint32_t fs;
  uint32_t es;
  uint32_t ds;
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t user_esp;
  uint32_t user_ss;
} perf_irq_frame_t;

void perf_boot_start(void);
void perf_sample_irq(const perf_irq_frame_t *frame);
void perf_boot_stop_and_dump(const char *reason);

#endif
