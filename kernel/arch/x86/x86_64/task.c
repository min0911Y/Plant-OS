#include <arch/x86/x86_64/cpu.h>
#include <dos.h>
#include <user_space.h>

void arch_thread_pointer_set(uintptr_t pointer) {
  x64_msr_write(0xc0000100, pointer);
}

void arch_task_context_init(arch_task_context_t *context, uintptr_t entry) {
  memset(context, 0, sizeof(*context));
  context->rip = entry;
}
void arch_task_fork_context_init(mtask *task) {
  x64_interrupt_frame_t *frame = (void *)(task->top - sizeof(*frame));
  frame->rax = 0;
  task->context = (void *)((uintptr_t)frame - sizeof(arch_task_context_t));
  arch_task_context_init(task->context, (uintptr_t)arch_task_interrupt_return);
}
void arch_task_enter_user(uintptr_t entry, uintptr_t stack, uintptr_t argument) {
  mtask *task = current_task();
  /* Use a local frame: the current C stack can overlap the TSS entry slot. */
  x64_interrupt_frame_t initial = {0};
  arch_fpu_reset(task);
  initial.simd = task->fpu_state.legacy;
  initial.rip = entry;
  initial.rdi = argument;
  initial.rsp = (stack & ~15ull) - 8;
  *(uint64_t *)initial.rsp = 0;
  initial.cs = 35;
  initial.ss = 27;
  initial.rflags = 0x202;
  initial.vector = 0;
  x64_return_to_user(&initial);
}

bool arch_task_prepare_signal(mtask *task, uintptr_t handler,
                              uintptr_t trampoline) {
  x64_interrupt_frame_t *frame = (void *)(task->top - sizeof(*frame));
  uintptr_t stack = (frame->rsp - 128 - sizeof(*frame)) & ~15ull;
  if (handler < USER_SPACE_START || handler >= USER_SPACE_END ||
      !x64_user_access(stack - 8, sizeof(*frame) + 8, true))
    return false;
  x64_interrupt_frame_t saved = *frame;
  saved.simd.reserved0 = 0;
  saved.simd.opcode &= 0x7ff;
  memset(saved.simd.reserved1, 0, sizeof(saved.simd.reserved1));
  memset(saved.simd.reserved2, 0, sizeof(saved.simd.reserved2));
  for (unsigned i = 0; i < 8; i++)
    memset(saved.simd.x87[i] + 10, 0, 6);
  if (!saved.simd.ymm_inuse)
    memset(saved.ymm_hi, 0, sizeof(saved.ymm_hi));
  *(x64_interrupt_frame_t *)stack = saved;
  *(uint64_t *)(stack - 8) = trampoline;
  frame->rsp = stack - 8;
  frame->rip = handler;
  return true;
}
