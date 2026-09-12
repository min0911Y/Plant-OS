#include <arch.h>
#include <arch/x86/i386/interrupt.h>
#include <dos.h>
#include <irq.h>
#include <string.h>

void arch_task_context_init(arch_task_context_t *context, uintptr_t entry) {
  memset(context, 0, sizeof(*context));
  context->eip = (uint32_t)entry;
}

void arch_task_fork_context_init(mtask *task) {
  uintptr_t address = task->top - sizeof(x86_interrupt_frame_t);
  x86_interrupt_frame_t *frame = (x86_interrupt_frame_t *)address;
  frame->eax = 0;
  address -= sizeof(arch_task_context_t);
  task->context = (arch_task_context_t *)address;
  arch_task_context_init(task->context,
                         (uintptr_t)arch_task_interrupt_return);
}

__attribute__((noreturn)) void
arch_task_enter_user(uintptr_t instruction_pointer, uintptr_t stack_top,
                     uintptr_t argument) {
  /* i386 SysV: the argument follows the return address; ESP + 4 is aligned. */
  uintptr_t stack_pointer = (stack_top & ~(uintptr_t)15) - 20;
  uintptr_t *stack = (uintptr_t *)stack_pointer;
  stack[0] = 0;
  stack[1] = argument;
  x86_interrupt_frame_t frame;
  x86_user_frame_init(&frame, (uint32_t)instruction_pointer,
                      (uint32_t)stack_pointer);
  x86_return_to_user(&frame);
}

_Static_assert(offsetof(mcontext_t, fpregs) == sizeof(x86_interrupt_frame_t),
               "i386 signal register prefix");
_Static_assert(sizeof(((mcontext_t *)0)->fpregs) == sizeof(arch_fpu_state_t),
               "i386 signal x87 image");

/* i386 keeps x87 state lazily, outside the interrupt stack frame. Flush the
 * owner before exporting it and invalidate the handler's live state on return. */
void x86_signal_dispatch(x86_interrupt_frame_t *frame, unsigned vector,
                          uintptr_t address, uintptr_t error) {
  if ((frame->cs & 3) != 3)
    return;
  irq_state_t flags = irq_save();
  mtask *task = current_task();
  if (vector >= 32 && !(task->signals.pending & ~task->signals.blocked)) {
    irq_restore(flags);
    return;
  }
  arch_fpu_handle_device_not_available(task);
  arch_fpu_flush_cpu();
  mcontext_t context = {0};
  memcpy(&context, frame, sizeof(*frame));
  memcpy(context.fpregs, &task->fpu_state, sizeof(task->fpu_state));
  arch_fpu_state_t *fp = (void *)context.fpregs;
  fp->reserved_control = fp->reserved_status = fp->reserved_tag = 0;
  fp->instruction_selector_opcode &= 0x07ffffff;
  fp->data_selector &= 0xffff;
  unsigned top_register = (fp->status >> 11) & 7;
  for (unsigned i = 0; i < 8; i++) {
    unsigned physical = (top_register + i) & 7;
    if (((fp->tag >> (physical * 2)) & 3) == 3)
      memset(fp->registers + i * 10, 0, 10);
  }
  if (vector < 32)
    user_signal_exception(&context, vector, address, error);
  else
    user_signal_dispatch(&context, NULL);
  memcpy(frame, &context, sizeof(*frame));
  irq_restore(flags);
}

bool x86_signal_restore(x86_interrupt_frame_t *frame, uintptr_t address) {
  mcontext_t context;
  if (!user_signal_restore(address, &context))
    return false;
  mtask *task = current_task();
  arch_fpu_reset(task);
  memcpy(&task->fpu_state, context.fpregs, sizeof(task->fpu_state));
  task->fpu_initialized = true;
  memcpy(frame, &context, sizeof(*frame));
  return true;
}
