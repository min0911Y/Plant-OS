#ifndef KERNEL_ARCH_X86_INTERRUPT_H
#define KERNEL_ARCH_X86_INTERRUPT_H

#include <ctypes.h>

typedef struct {
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
  uint32_t esp;
  uint32_t ss;
} x86_interrupt_frame_t;

void x86_syscall_entry(void);
void x86_custom_syscall_entry(void);
__attribute__((noreturn)) void
x86_return_to_user(const x86_interrupt_frame_t *frame);
void x86_syscall_dispatch(x86_interrupt_frame_t *frame);
void x86_custom_syscall_dispatch(x86_interrupt_frame_t *frame);

#endif
