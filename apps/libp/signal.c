#include <errno.h>
#include <signal.h>

static int signal_result(intptr_t result) {
  if (result < 0) {
    errno = -result;
    return -1;
  }
  return result;
}
int sigaction(int sig, const struct sigaction *action, struct sigaction *old) {
  return signal_result(signal_call(SIGNAL_ACTION, sig, (uintptr_t)action,
                                   (uintptr_t)old));
}
sighandler_t signal(int sig, sighandler_t handler) {
  struct sigaction action = {.sa_handler = handler}, old;
  return sigaction(sig, &action, &old) ? SIG_ERR : old.sa_handler;
}
int sigemptyset(sigset_t *set) {
  *set = 0;
  return 0;
}
int sigfillset(sigset_t *set) {
  *set = 0x7fffffff;
  return 0;
}
int sigaddset(sigset_t *set, int sig) {
  if (sig <= 0 || sig >= NSIG)
    return signal_result(-EINVAL);
  *set |= SIGMASK(sig);
  return 0;
}
int sigdelset(sigset_t *set, int sig) {
  if (sig <= 0 || sig >= NSIG)
    return signal_result(-EINVAL);
  *set &= ~SIGMASK(sig);
  return 0;
}
int sigismember(const sigset_t *set, int sig) {
  if (sig <= 0 || sig >= NSIG)
    return signal_result(-EINVAL);
  return !!(*set & SIGMASK(sig));
}
int sigprocmask(int how, const sigset_t *set, sigset_t *old) {
  return signal_result(signal_call(SIGNAL_MASK, how, (uintptr_t)set,
                                   (uintptr_t)old));
}
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) {
  return -signal_call(SIGNAL_MASK, how, (uintptr_t)set, (uintptr_t)old);
}
int sigpending(sigset_t *set) {
  return signal_result(signal_call(SIGNAL_PENDING, (uintptr_t)set, 0, 0));
}
int sigaltstack(const stack_t *stack, stack_t *old) {
  return signal_result(signal_call(SIGNAL_STACK, (uintptr_t)stack,
                                   (uintptr_t)old, 0));
}
int raise(int sig) {
  if (sig <= 0 || sig >= NSIG)
    return signal_result(-EINVAL);
  return signal_result(signal_call(SIGNAL_RAISE, sig, 0, 0));
}
