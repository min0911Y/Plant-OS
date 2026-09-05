#include <arch.h>
#include <arch/x86/i386/interrupt.h>
#include <dos.h>
#include <string.h>

typedef struct {
  uint32_t trampoline;
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
  uint32_t instruction_pointer;
} i386_signal_frame_t;

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

bool arch_task_prepare_signal(mtask *task, uintptr_t handler,
                              uintptr_t trampoline) {
  if (task == NULL || handler == 0 || trampoline == 0) {
    return false;
  }

  x86_interrupt_frame_t *interrupt_frame =
      (x86_interrupt_frame_t *)(task->top - sizeof(x86_interrupt_frame_t));
  i386_signal_frame_t *signal_frame =
      (i386_signal_frame_t *)(uintptr_t)(interrupt_frame->esp -
                                         sizeof(i386_signal_frame_t));
  signal_frame->edi = interrupt_frame->edi;
  signal_frame->esi = interrupt_frame->esi;
  signal_frame->ebp = interrupt_frame->ebp;
  signal_frame->esp_dummy = interrupt_frame->esp_dummy;
  signal_frame->ebx = interrupt_frame->ebx;
  signal_frame->ecx = interrupt_frame->ecx;
  signal_frame->edx = interrupt_frame->edx;
  signal_frame->eax = interrupt_frame->eax;
  signal_frame->gs = interrupt_frame->gs;
  signal_frame->fs = interrupt_frame->fs;
  signal_frame->es = interrupt_frame->es;
  signal_frame->ds = interrupt_frame->ds;
  signal_frame->instruction_pointer = interrupt_frame->eip;
  signal_frame->trampoline = (uint32_t)trampoline;
  interrupt_frame->eip = (uint32_t)handler;
  interrupt_frame->esp = (uint32_t)(uintptr_t)signal_frame;
  return true;
}
