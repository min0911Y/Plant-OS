#ifndef KERNEL_ARCH_X86_BIOS_H
#define KERNEL_ARCH_X86_BIOS_H

#include <ctypes.h>

typedef struct {
  uint16_t di, si, bp, sp, bx, dx, cx, ax;
  uint16_t gs, fs, es, ds, eflags;
} regs16_t;

void x86_bios_interrupt(uint8_t interrupt_number, regs16_t *registers);

#endif
