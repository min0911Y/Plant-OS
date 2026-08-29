#ifndef KERNEL_ARCH_X86_INTERRUPT_H
#define KERNEL_ARCH_X86_INTERRUPT_H

#include <ctypes.h>
#include <stddef.h>

enum {
  X86_EXCEPTION_COUNT = 32,
  X86_VECTOR_RESCHEDULE = 0xf0,
};

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
  uint32_t vector;
  uint32_t error;
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  /* Only present when the interrupted CS has RPL 3. */
  uint32_t useresp;
  uint32_t userss;
} x86_exception_frame_t;

_Static_assert(offsetof(x86_exception_frame_t, edi) == 0,
               "x86 exception edi offset");
_Static_assert(offsetof(x86_exception_frame_t, gs) == 32,
               "x86 exception gs offset");
_Static_assert(offsetof(x86_exception_frame_t, vector) == 48,
               "x86 exception vector offset");
_Static_assert(offsetof(x86_exception_frame_t, error) == 52,
               "x86 exception error offset");
_Static_assert(offsetof(x86_exception_frame_t, eip) == 56,
               "x86 exception eip offset");
_Static_assert(offsetof(x86_exception_frame_t, useresp) == 68,
               "x86 exception base frame size");
_Static_assert(sizeof(x86_exception_frame_t) == 76,
               "x86 exception user frame size");

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

void x86_user_frame_init(x86_interrupt_frame_t *frame, uint32_t eip,
                         uint32_t esp);

void null_inthandler(void);
extern void (*const x86_exception_entries[X86_EXCEPTION_COUNT])(void);
void x86_exception_dispatch(x86_exception_frame_t *frame);
void asm_inthandler20(void);
void asm_inthandler21(void);
void asm_inthandler2c(void);
void asm_ide_irq(void);
void floppy_int(void);
void PCNET_ASM_INTHANDLER(void);
void RTL8139_ASM_INTHANDLER(void);
void asm_rtc_handler(void);
void asm_sb16_handler(void);
void x86_syscall_entry(void);
void x86_reschedule_entry(void);
__attribute__((noreturn)) void
x86_return_to_user(const x86_interrupt_frame_t *frame);
void x86_syscall_dispatch(x86_interrupt_frame_t *frame);

#endif
