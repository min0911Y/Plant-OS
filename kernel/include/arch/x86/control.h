#ifndef KERNEL_ARCH_X86_CONTROL_H
#define KERNEL_ARCH_X86_CONTROL_H

#include <ctypes.h>
#include <stddef.h>

#define X86_CR0_PE (1u << 0)
#define X86_CR0_MP (1u << 1)
#define X86_CR0_EM (1u << 2)
#define X86_CR0_TS (1u << 3)
#define X86_CR0_ET (1u << 4)
#define X86_CR0_NE (1u << 5)
#define X86_CR0_WP (1u << 16)
#define X86_CR0_AM (1u << 18)
#define X86_CR0_NW (1u << 29)
#define X86_CR0_CD (1u << 30)
#define X86_CR0_PG (1u << 31)

static inline uint32_t x86_cr0_read(void) {
  uint32_t value;
  asm volatile("movl %%cr0, %0" : "=r"(value));
  return value;
}

static inline void x86_cr0_write(uint32_t value) {
  asm volatile("movl %0, %%cr0" : : "r"(value) : "memory");
}

static inline uintptr_t x86_cr2_read(void) {
  uintptr_t value;
  asm volatile("movl %%cr2, %0" : "=r"(value));
  return value;
}

static inline void x86_cr3_write(uintptr_t value) {
  asm volatile("movl %0, %%cr3" : : "r"(value) : "memory");
}

static inline void x86_fpu_disable(void) {
  x86_cr0_write(x86_cr0_read() | X86_CR0_EM | X86_CR0_TS);
}

#endif
