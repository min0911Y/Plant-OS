#include <arch/x86/i386/interrupt.h>
#include <dos.h>
#include <irq.h>
#include <syscall.h>

void x86_syscall_dispatch(x86_interrupt_frame_t *frame) {
  if (frame->eax == SYSCALL_ARCH_SIGNAL_RETURN) {
    if (!x86_signal_restore(frame, frame->ebx))
      task_exit_process((unsigned)-1);
    x86_signal_dispatch(frame, 32, 0, 0);
    return;
  }
  syscall_context_t context = {
      .value = frame->eax,
      .argument0 = frame->ebx,
      .argument1 = frame->ecx,
      .argument2 = frame->edx,
      .argument3 = frame->esi,
      .argument4 = frame->edi,
      .argument5 = frame->ebp,
  };

  irq_enable();
  syscall_dispatch(&context);

  frame->eax = (uint32_t)context.value;
  frame->ebx = (uint32_t)context.argument0;
  frame->ecx = (uint32_t)context.argument1;
  frame->edx = (uint32_t)context.argument2;
  frame->esi = (uint32_t)context.argument3;
  frame->edi = (uint32_t)context.argument4;
  frame->ebp = (uint32_t)context.argument5;
  x86_signal_dispatch(frame, 32, 0, 0);
}
