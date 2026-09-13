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
  SIGSTKFLT = 16, SIGCHLD = 17, SIGCONT = 18, SIGSTOP = 19,
  SIGTSTP = 20, SIGTTIN = 21, SIGTTOU = 22, SIGURG = 23,
  SIGXCPU = 24, SIGXFSZ = 25, SIGVTALRM = 26, SIGPROF = 27,
  SIGWINCH = 28, SIGIO = 29, SIGPWR = 30, SIGSYS = 31,
  NSIG = 32
};
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGMASK(n) ((sigset_t)1 << ((n) - 1))
enum { SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK };
enum {
  SA_SIGINFO = 1,
  SA_ONSTACK = 2,
  SA_NODEFER = 4,
  SA_RESETHAND = 8,
  SA_NOCLDSTOP = 16,
  SA_NOCLDWAIT = 32,
  SA_RESTART = 64,
};
enum { SS_ONSTACK = 1, SS_DISABLE = 2, MINSIGSTKSZ = 4096, SIGSTKSZ = 16384 };
enum {
  SI_USER = 0,
  SI_QUEUE = -1,
  SI_TIMER = -2,
  SI_MESGQ = -3,
  SI_ASYNCIO = -4,
  SI_TKILL = -6,
};
enum { SEGV_MAPERR = 1, SEGV_ACCERR = 2 };
enum {
  ILL_ILLOPC = 1,
  ILL_ILLOPN = 2,
  ILL_ILLADR = 3,
  ILL_ILLTRP = 4,
  ILL_PRVOPC = 5,
  ILL_PRVREG = 6,
  ILL_COPROC = 7,
  ILL_BADSTK = 8,
};
enum { FPE_INTDIV = 1, FPE_INTOVF = 2, FPE_FLTDIV = 3, FPE_FLTOVF = 4,
       FPE_FLTUND = 5, FPE_FLTRES = 6, FPE_FLTINV = 7, FPE_FLTSUB = 8 };
enum { BUS_ADRALN = 1, BUS_ADRERR = 2, BUS_OBJERR = 3 };
enum {
  CLD_EXITED = 1,
  CLD_KILLED = 2,
  CLD_DUMPED = 3,
  CLD_TRAPPED = 4,
  CLD_STOPPED = 5,
  CLD_CONTINUED = 6,
};
enum { TRAP_BRKPT = 1, TRAP_TRACE = 2 };
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
typedef struct {
  int si_signo, si_code, si_errno;
  union {
    void *si_addr;
    struct {
      int32_t si_pid;
      uint32_t si_uid;
    };
    int si_status;
  };
} siginfo_t;
struct sigaction {
  union {
    sighandler_t sa_handler;
    void (*sa_sigaction)(int, siginfo_t *, void *);
  };
  sigset_t sa_mask;
  int sa_flags;
};
enum {
  SIGNAL_ACTION,
  SIGNAL_MASK,
  SIGNAL_STACK,
  SIGNAL_PENDING,
  SIGNAL_RAISE,
  SIGNAL_SEND,
  SIGNAL_KILL,
  SIGNAL_SUSPEND,
  SIGNAL_OPERATION_COUNT
};
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
int sigsuspend(const sigset_t *mask);
/* my_basic's freestanding shell uses a private callback named raise. */
#ifndef MB_FREESTANDING
int raise(int sig);
#endif
int kill(int32_t pid, int sig);
intptr_t signal_call(unsigned operation, uintptr_t a, uintptr_t b, uintptr_t c);
#ifdef __cplusplus
}
#endif
#endif
