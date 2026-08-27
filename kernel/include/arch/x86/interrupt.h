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

void x86_user_frame_init(x86_interrupt_frame_t *frame, uint32_t eip,
                         uint32_t esp);

void null_inthandler(void);
void asm_error0(void);
void asm_error1(void);
void asm_error3(void);
void asm_error4(void);
void asm_error5(void);
void asm_error6(void);
void asm_error7(void);
void asm_error8(void);
void asm_error9(void);
void asm_error10(void);
void asm_error11(void);
void asm_error12(void);
void asm_error13(void);
void asm_error14(void);
void asm_error16(void);
void asm_error17(void);
void asm_error18(void);
void asm_inthandler20(void);
void asm_inthandler21(void);
void asm_inthandler2c(void);
void asm_ide_irq(void);
void asm_net_api(void);
void floppy_int(void);
void PCNET_ASM_INTHANDLER(void);
void RTL8139_ASM_INTHANDLER(void);
void asm_rtc_handler(void);
void asm_sb16_handler(void);
void x86_syscall_entry(void);
void x86_custom_syscall_entry(void);
__attribute__((noreturn)) void
x86_return_to_user(const x86_interrupt_frame_t *frame);
void x86_syscall_dispatch(x86_interrupt_frame_t *frame);
void x86_custom_syscall_dispatch(x86_interrupt_frame_t *frame);

#endif
