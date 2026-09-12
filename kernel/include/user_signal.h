#ifndef KERNEL_USER_SIGNAL_H
#define KERNEL_USER_SIGNAL_H
#include <signal_context.h>
struct mtask;
typedef struct {
  sigset_t pending, blocked;
  stack_t stack;
  uintptr_t frame;
  bool onstack;
} task_signal_state_t;
intptr_t user_signal_operation(unsigned operation, uintptr_t a, uintptr_t b,
                               uintptr_t c);
bool user_signal_dispatch(mcontext_t *context, const siginfo_t *fault);
bool user_signal_restore(uintptr_t address, mcontext_t *context);
void user_signal_exception(mcontext_t *context, unsigned vector,
                           uintptr_t address, uintptr_t error);
void user_signal_send(struct mtask *task, int sig);
#endif
