#ifndef PLANT_SIGNAL_H
#define PLANT_SIGNAL_H
#include <ctypes.h>

typedef int sig_atomic_t;
typedef uint32_t sigset_t;
typedef void (*sighandler_t)(int);
enum {
  SIGHUP = 1, SIGINT = 2, SIGQUIT = 3, SIGILL = 4, SIGTRAP = 5,
  SIGABRT = 6, SIGBUS = 7, SIGFPE = 8, SIGKILL = 9, SIGUSR1 = 10,
  SIGSEGV = 11, SIGUSR2 = 12, SIGPIPE = 13, SIGALRM = 14, SIGTERM = 15,
  NSIG = 32
};
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGMASK(n) ((sigset_t)1 << ((n) - 1))
enum { SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK };
enum { SA_SIGINFO = 1, SA_ONSTACK = 2, SA_NODEFER = 4, SA_RESETHAND = 8 };
enum { SS_ONSTACK = 1, SS_DISABLE = 2, MINSIGSTKSZ = 4096, SIGSTKSZ = 16384 };
enum { SI_USER = 0, SI_TKILL = -6 };
enum { SEGV_MAPERR = 1, SEGV_ACCERR = 2 };
enum { ILL_ILLOPC = 1 };
enum { FPE_INTDIV = 1, FPE_INTOVF = 2, FPE_FLTDIV = 3, FPE_FLTOVF = 4,
       FPE_FLTUND = 5, FPE_FLTRES = 6, FPE_FLTINV = 7 };
enum { BUS_ADRALN = 1, TRAP_BRKPT = 1, TRAP_TRACE = 2 };
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
typedef struct {
  int si_signo, si_code, si_errno;
  void *si_addr;
} siginfo_t;
struct sigaction {
  union {
    sighandler_t sa_handler;
    void (*sa_sigaction)(int, siginfo_t *, void *);
  };
  sigset_t sa_mask;
  int sa_flags;
};
enum { SIGNAL_ACTION, SIGNAL_MASK, SIGNAL_STACK, SIGNAL_PENDING, SIGNAL_RAISE,
       SIGNAL_SEND, SIGNAL_OPERATION_COUNT };
#ifdef __cplusplus
extern "C" {
#endif
int sigaction(int sig, const struct sigaction *action, struct sigaction *old);
sighandler_t signal(int sig, sighandler_t handler);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigdelset(sigset_t *set, int sig);
int sigismember(const sigset_t *set, int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *old);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old);
int sigpending(sigset_t *set);
int sigaltstack(const stack_t *stack, stack_t *old);
int raise(int sig);
intptr_t signal_call(unsigned operation, uintptr_t a, uintptr_t b, uintptr_t c);
#ifdef __cplusplus
}
#endif
#endif
