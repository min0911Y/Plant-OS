#include <dos.h>
#include <irq.h>
#include <stdint.h>
#include <user_signal.h>
#include <user_space.h>
#include <user_vm.h>
#if defined(KERNEL_ARCH_X86_64)
#include <arch/x86/x86_64/cpu.h>
#else
#include <arch/x86/i386/interrupt.h>
#endif

_Static_assert(sizeof(struct sigaction) == (sizeof(uintptr_t) == 8 ? 16 : 12),
               "native sigaction ABI");
_Static_assert(sizeof(stack_t) == (sizeof(uintptr_t) == 8 ? 24 : 12),
               "native signal stack ABI");
_Static_assert(sizeof(siginfo_t) == (sizeof(uintptr_t) == 8 ? 24 : 20),
               "native siginfo ABI");

#define SIGNAL_BITS ((sigset_t)0x7fffffff)
#define BLOCKABLE_BITS (SIGNAL_BITS & ~SIGMASK(SIGKILL))

typedef struct {
  ucontext_t context;
  siginfo_t information;
  uintptr_t previous;
  unsigned onstack;
} signal_frame_t;

void user_signal_send(mtask *task, int sig) {
  mtask *owner = get_task(task->tgid);
  if (sig == SIGKILL) {
    task_kill(task->tgid);
    return;
  }
  if (!owner || owner->signal_actions[sig].sa_handler == SIG_IGN)
    return;
  task->signals.pending |= SIGMASK(sig);
  /* Futex waits explicitly return EINTR. Other wait APIs retain their current
   * completion semantics; do not tear down arbitrary resource wait queues. */
  if (!(task->signals.blocked & SIGMASK(sig)) &&
      (task->wait_reason == WAIT_REASON_FUTEX ||
       task->wait_reason == WAIT_REASON_SIGNAL))
    task_run(task);
}

intptr_t user_signal_operation(unsigned operation, uintptr_t a, uintptr_t b,
                               uintptr_t c) {
  irq_state_t flags = irq_save();
  mtask *task = current_task();
  mtask *owner = get_task(task->tgid);
  task_signal_state_t *state = &task->signals;
  intptr_t result = -22;
  switch (operation) {
  case SIGNAL_ACTION: {
    struct sigaction action;
    if (!a || a >= NSIG || a == SIGKILL)
      break;
    if (b && !user_vm_copy_from(&action, b, sizeof(action))) {
      result = -14;
      break;
    }
    if (b && (action.sa_flags & ~(SA_SIGINFO | SA_ONSTACK | SA_NODEFER |
                                  SA_RESETHAND | SA_NOCLDSTOP | SA_NOCLDWAIT |
                                  SA_RESTART) ||
              (action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN &&
               !user_vm_readable((uintptr_t)action.sa_handler, 1))))
      break;
    if (c && !user_vm_copy_to(c, &owner->signal_actions[a], sizeof(action))) {
      result = -14;
      break;
    }
    if (b) {
      action.sa_mask &= BLOCKABLE_BITS;
      owner->signal_actions[a] = action;
      if (action.sa_handler == SIG_IGN) {
        task_iterator_t iterator = {0};
        mtask *thread;
        while ((thread = task_iter_next(&iterator))) {
          if (thread->tgid == task->tgid)
            thread->signals.pending &= ~SIGMASK(a);
        }
      }
    }
    result = 0;
    break;
  }
  case SIGNAL_MASK: {
    sigset_t mask;
    if (b && (a > SIG_SETMASK || !user_vm_copy_from(&mask, b, sizeof(mask)))) {
      result = a > SIG_SETMASK ? -22 : -14;
      break;
    }
    if (c && !user_vm_copy_to(c, &state->blocked, sizeof(state->blocked))) {
      result = -14;
      break;
    }
    if (b) {
      mask &= BLOCKABLE_BITS;
      state->blocked = a == SIG_BLOCK ? state->blocked | mask
                       : a == SIG_UNBLOCK ? state->blocked & ~mask : mask;
    }
    result = 0;
    break;
  }
  case SIGNAL_STACK: {
    stack_t stack;
    if (a && !user_vm_copy_from(&stack, a, sizeof(stack))) {
      result = -14;
      break;
    }
    if (a && state->onstack) {
      result = -1;
      break;
    }
    if (a && stack.ss_flags != 0 && stack.ss_flags != SS_DISABLE)
      break;
    if (a && !stack.ss_flags) {
      if (stack.ss_size < MINSIGSTKSZ) {
        result = -12;
        break;
      }
      if (!user_vm_prepare_write((uintptr_t)stack.ss_sp, stack.ss_size)) {
        result = -14;
        break;
      }
    }
    stack_t old = state->stack;
    old.ss_flags = state->onstack ? SS_ONSTACK : old.ss_size ? 0 : SS_DISABLE;
    if (b && !user_vm_copy_to(b, &old, sizeof(old))) {
      result = -14;
      break;
    }
    if (a)
      state->stack = stack.ss_flags == SS_DISABLE ? (stack_t){0} : stack;
    result = 0;
    break;
  }
  case SIGNAL_PENDING:
    result = user_vm_copy_to(a, &state->pending, sizeof(state->pending)) ? 0 : -14;
    break;
  case SIGNAL_RAISE:
  case SIGNAL_SEND: {
    if (a >= NSIG || (operation == SIGNAL_SEND && b != (uint32_t)b))
      break;
    mtask *target = operation == SIGNAL_RAISE ? task : get_task(b);
    if (!target || target->tgid != task->tgid ||
        (operation == SIGNAL_SEND && target->generation != c) ||
        target->state == EMPTY || target->state == DIED ||
        target->state == WILL_EMPTY || target->state == ALLOCATING) {
      result = -3;
      break;
    }
    if (a)
      user_signal_send(target, a);
    result = 0;
    break;
  }
  case SIGNAL_KILL: {
    if (a >= NSIG || b > UINT32_MAX || (int32_t)(uint32_t)b <= 0) {
      result = b > UINT32_MAX || (int32_t)(uint32_t)b <= 0 ? -95 : -22;
      break;
    }
    mtask *target = get_task((uint32_t)b);
    if (!target || target->tid != target->tgid || target->state == DIED) {
      result = -3;
      break;
    }
    if (a)
      user_signal_send(target, (int)a);
    result = 0;
    break;
  }
  case SIGNAL_SUSPEND: {
    sigset_t mask;
    if (!a || !user_vm_copy_from(&mask, a, sizeof(mask))) {
      result = -14;
      break;
    }
    sigset_t previous = state->blocked;
    state->blocked = mask & BLOCKABLE_BITS;
    if (!(state->pending & ~state->blocked))
      task_fall_blocked_reason(WAITING, WAIT_REASON_SIGNAL);

    sigset_t pending = state->pending & ~state->blocked;
    if (pending) {
      unsigned sig = pending & SIGMASK(SIGKILL)
                         ? SIGKILL
                         : __builtin_ctz(pending) + 1;
      if (previous & SIGMASK(sig))
        state->pending &= ~SIGMASK(sig);
    }
    state->blocked = previous;
    result = -4;
    break;
  }
  }
  irq_restore(flags);
  return result;
}

bool user_signal_dispatch(mcontext_t *context, const siginfo_t *fault) {
  mtask *task = current_task();
  if ((context->cs & 3) != 3 || task->tid == NULL_TID)
    return false;
  irq_state_t flags = irq_save();
  task_signal_state_t *state = &task->signals;
  sigset_t pending = state->pending & ~state->blocked;
  siginfo_t information = {0};
  if (fault) {
    information = *fault;
  } else {
    if (!pending) {
      irq_restore(flags);
      return false;
    }
    information.si_signo = pending & SIGMASK(SIGKILL)
                               ? SIGKILL : __builtin_ctz(pending) + 1;
    information.si_code = SI_TKILL;
    state->pending &= ~SIGMASK(information.si_signo);
  }
  int sig = information.si_signo;
  mtask *owner = get_task(task->tgid);
  struct sigaction action = owner->signal_actions[sig];
  if (!fault && action.sa_handler == SIG_IGN) {
    irq_restore(flags);
    return false;
  }
  if (action.sa_handler == SIG_DFL || action.sa_handler == SIG_IGN ||
      (fault && (state->blocked & SIGMASK(sig))) || sig == SIGKILL)
    task_exit_process((unsigned)-1);

  signal_frame_t saved = {0};
  saved.context.uc_mcontext = *context;
  saved.context.uc_sigmask = state->blocked;
  saved.context.uc_stack = state->stack;
  saved.context.uc_stack.ss_flags = state->onstack ? SS_ONSTACK
                                   : state->stack.ss_size ? 0 : SS_DISABLE;
  saved.information.si_signo = information.si_signo;
  saved.information.si_code = information.si_code;
  saved.information.si_errno = information.si_errno;
  saved.information.si_addr = information.si_addr;
  saved.previous = state->frame;
  saved.onstack = state->onstack;
#if defined(KERNEL_ARCH_X86_64)
  signal_fpregs_t *fp = &saved.context.uc_mcontext.simd;
  fp->reserved0 = 0;
  fp->opcode &= 0x7ff;
  memset(fp->reserved1, 0, sizeof(fp->reserved1));
  memset(fp->reserved2, 0, sizeof(fp->reserved2));
  unsigned top_register = (fp->status >> 11) & 7;
  for (unsigned i = 0; i < 8; i++) {
    unsigned physical = (top_register + i) & 7;
    if (!(fp->tag & (1u << physical)))
      memset(fp->x87[i], 0, 10);
    memset(fp->x87[i] + 10, 0, 6);
  }
  if (!fp->ymm_inuse)
    memset(saved.context.uc_mcontext.ymm_hi, 0,
           sizeof(saved.context.uc_mcontext.ymm_hi));
  uintptr_t top = context->rsp;
  const size_t arguments = 8;
#else
  uintptr_t top = context->esp;
  const size_t arguments = 20;
#endif
  bool onstack = state->onstack ||
                 ((action.sa_flags & SA_ONSTACK) && state->stack.ss_size);
  if (onstack && !state->onstack)
    top = (uintptr_t)state->stack.ss_sp + state->stack.ss_size;
  if (top < sizeof(saved) + arguments)
    task_exit_process((unsigned)-1);
  uintptr_t address = (top - sizeof(saved)) & ~(uintptr_t)15;
  uintptr_t stack = address - arguments;
  if ((onstack && stack < (uintptr_t)state->stack.ss_sp) ||
      !user_vm_readable(owner->ret_to_app, 1) ||
      !user_vm_prepare_write(stack, top - stack))
    task_exit_process((unsigned)-1);
  memcpy((void *)address, &saved, sizeof(saved));
  uintptr_t *slots = (void *)stack;
  slots[0] = owner->ret_to_app;
#if defined(KERNEL_ARCH_X86_64)
  context->rsp = stack;
  context->rip = (uintptr_t)action.sa_handler;
  context->rdi = sig;
  context->rsi = address + offsetof(signal_frame_t, information);
  context->rdx = address;
  context->rflags &= ~((uintptr_t)0x100 | 0x400 | 0x40000);
#else
  slots[1] = sig;
  slots[2] = address + offsetof(signal_frame_t, information);
  slots[3] = address;
  slots[4] = address;
  context->esp = stack;
  context->eip = (uintptr_t)action.sa_handler;
  context->eflags &= ~(0x100u | 0x400u | 0x40000u);
#endif
  state->frame = address;
  state->onstack = onstack;
  state->blocked |= action.sa_mask;
  if (!(action.sa_flags & SA_NODEFER))
    state->blocked |= SIGMASK(sig);
  if (action.sa_flags & SA_RESETHAND)
    owner->signal_actions[sig] = (struct sigaction){0};
  irq_restore(flags);
  return true;
}

bool user_signal_restore(uintptr_t address, mcontext_t *context) {
  task_signal_state_t *state = &current_task()->signals;
  signal_frame_t saved;
  if (!address || address != state->frame ||
      !user_vm_copy_from(&saved, address, sizeof(saved)) || saved.onstack > 1)
    return false;
  mcontext_t *registers = &saved.context.uc_mcontext;
#if defined(KERNEL_ARCH_X86_64)
  if (registers->rip < USER_SPACE_START || registers->rip >= USER_HEAP_END ||
      registers->rsp < USER_SPACE_START || registers->rsp >= USER_HEAP_END ||
      (registers->simd.mxcsr & ~x64_mxcsr_mask) || registers->simd.ymm_inuse > 1 ||
      (registers->simd.ymm_inuse && !(x64_xstate_mask & 4)))
    return false;
  registers->cs = 35;
  registers->ss = 27;
  registers->rflags = (registers->rflags & 0x240dd5) | 0x202;
  registers->vector = 0;
  registers->error = 0;
#else
  if (registers->eip < USER_SPACE_START || registers->eip >= USER_HEAP_END ||
      registers->esp < USER_SPACE_START || registers->esp >= USER_HEAP_END)
    return false;
  x86_interrupt_frame_t clean;
  x86_user_frame_init(&clean, registers->eip, registers->esp);
  registers->cs = clean.cs;
  registers->ss = clean.ss;
  registers->ds = clean.ds;
  registers->es = clean.es;
  registers->fs = clean.fs;
  registers->gs = clean.gs;
  registers->eflags = (registers->eflags & 0x240dd5) | 0x202;
#endif
  *context = *registers;
  state->blocked = saved.context.uc_sigmask & BLOCKABLE_BITS;
  state->frame = saved.previous;
  state->onstack = saved.onstack;
  return true;
}

void user_signal_exception(mcontext_t *context, unsigned vector,
                           uintptr_t address, uintptr_t error) {
  siginfo_t information = {.si_signo = SIGSEGV, .si_code = SEGV_ACCERR,
                          .si_addr = (void *)address};
  switch (vector) {
  case 0:
    information.si_signo = SIGFPE;
    information.si_code = FPE_INTDIV;
    break;
  case 1:
    information.si_signo = SIGTRAP;
    information.si_code = TRAP_TRACE;
    break;
  case 3:
    information.si_signo = SIGTRAP;
    information.si_code = TRAP_BRKPT;
    information.si_addr = (void *)(address - 1);
    break;
  case 6:
    information.si_signo = SIGILL;
    information.si_code = ILL_ILLOPC;
    break;
  case 14:
    information.si_code =
        (error & 1) || arch_user_page_flags(address & ~(uintptr_t)(VM_PAGE_SIZE - 1))
            ? SEGV_ACCERR : SEGV_MAPERR;
    break;
  case 16:
  case 19: {
#if defined(KERNEL_ARCH_X86_64)
    unsigned exceptions = vector == 19
                              ? context->simd.mxcsr & ~(context->simd.mxcsr >> 7)
                              : context->simd.status & ~context->simd.control;
#else
    const arch_fpu_state_t *fp = (const void *)context->fpregs;
    unsigned exceptions = fp->status & ~fp->control;
#endif
    information.si_signo = SIGFPE;
    information.si_code = exceptions & 1 ? FPE_FLTINV
                          : exceptions & 4 ? FPE_FLTDIV
                          : exceptions & 8 ? FPE_FLTOVF
                          : exceptions & 0x12 ? FPE_FLTUND
                          : exceptions & 0x20 ? FPE_FLTRES : FPE_FLTINV;
    break;
  }
  case 17:
    information.si_signo = SIGBUS;
    information.si_code = BUS_ADRALN;
    break;
  }
  user_signal_dispatch(context, &information);
}
