#include <errno.h>
#include <futex.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <syscall.h>
#include <ucontext.h>

enum { RECOVER_PAGE, RECOVER_OPCODE, BAD_STACK, BAD_SIMD, NESTED, RESET };
static _Thread_local int mode;
static _Thread_local volatile sig_atomic_t calls, handler_errors, depth;
static _Thread_local void *fault_page;
static _Thread_local stack_t alternate;
static uint32_t worker_ready, worker_go, worker_wait;
static int failures;

#define CHECK(condition) do { \
  if (!(condition)) { \
    logkf("SIGNALTEST FAIL line=%d: %s\n", __LINE__, #condition); \
    failures++; \
  } \
} while (0)

static void handler(int sig, siginfo_t *information, void *opaque) {
  ucontext_t *context = opaque;
  calls++;
  if (information->si_signo != sig)
    handler_errors++;
  if (alternate.ss_size) {
    stack_t current;
    uintptr_t local = (uintptr_t)&current;
    if (local < (uintptr_t)alternate.ss_sp ||
        local >= (uintptr_t)alternate.ss_sp + alternate.ss_size ||
        sigaltstack(NULL, &current) || current.ss_flags != SS_ONSTACK)
      handler_errors++;
    stack_t disabled = {.ss_flags = SS_DISABLE};
    if (sigaltstack(&disabled, NULL) != -1 || errno != EPERM)
      handler_errors++;
  }
  if (sig == SIGUSR2)
    return;
  if (sig == SIGTRAP) {
#if __SIZEOF_POINTER__ == 8
    uintptr_t pc = context->uc_mcontext.rip;
#else
    uintptr_t pc = context->uc_mcontext.eip;
#endif
    if (information->si_code != TRAP_BRKPT ||
        (uintptr_t)information->si_addr != pc - 1)
      handler_errors++;
    return;
  }
  if (sig == SIGUSR1) {
    if (mode == NESTED && depth++ == 0)
      raise(SIGUSR1);
    return;
  }
  if (sig == SIGFPE && information->si_code != FPE_INTDIV)
    handler_errors++;
  if (mode == RECOVER_PAGE) {
    if (sig != SIGSEGV || ((uintptr_t)information->si_addr - (uintptr_t)fault_page >= 4096) ||
        information->si_code != SEGV_ACCERR ||
        mprotect(fault_page, 4096, PROT_READ | PROT_WRITE))
      handler_errors++;
    raise(SIGUSR2);
    return;
  }
#if __SIZEOF_POINTER__ == 8
  context->uc_mcontext.rip += 2; /* UD2 */
  context->uc_mcontext.rax = 0x12345678;
  if (mode == BAD_STACK)
    context->uc_mcontext.rsp = 0;
  if (mode == BAD_SIMD)
    context->uc_mcontext.simd.mxcsr = 0xffffffff;
  __asm__ volatile("pxor %%xmm0, %%xmm0" ::: "xmm0");
#else
  context->uc_mcontext.eip += 2;
  context->uc_mcontext.eax = 0x12345678;
  if (mode == BAD_STACK)
    context->uc_mcontext.esp = 0;
#endif
  __asm__ volatile("fninit" ::: "memory");
}

static void install(int sig, int flags) {
  struct sigaction action = {.sa_sigaction = handler,
                            .sa_flags = SA_SIGINFO | flags};
  CHECK(sigaction(sig, &action, NULL) == 0);
}

static void context_tests(void) {
  alternate = (stack_t){.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
                       .ss_size = SIGSTKSZ};
  CHECK(alternate.ss_sp != MAP_FAILED);
  CHECK(sigaltstack(&alternate, NULL) == 0);
  install(SIGSEGV, SA_ONSTACK);
  install(SIGILL, SA_ONSTACK);
  install(SIGFPE, SA_ONSTACK);
  install(SIGTRAP, SA_ONSTACK);
  install(SIGUSR2, SA_ONSTACK);
  fault_page = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(fault_page != MAP_FAILED);
  mode = RECOVER_PAGE;
  calls = 0;
  *(volatile unsigned *)fault_page = 42;
  CHECK(*(volatile unsigned *)fault_page == 42 && calls == 2);
  CHECK(handler_errors == 0);
  CHECK(mprotect(fault_page, 4096, PROT_NONE) == 0);
  uintptr_t guard_top = (uintptr_t)fault_page + 4096;
  calls = 0;
#if __SIZEOF_POINTER__ == 8
  __asm__ volatile("mov %%rsp, %%r11; mov %0, %%rsp; pushq $1; popq %%rax; mov %%r11, %%rsp"
                   :: "r"(guard_top) : "rax", "r11", "memory");
#else
  __asm__ volatile("mov %%esp, %%edx; mov %0, %%esp; pushl $1; popl %%eax; mov %%edx, %%esp"
                   :: "r"(guard_top) : "eax", "edx", "memory");
#endif
  CHECK(calls == 2 && handler_errors == 0);
  CHECK(munmap(fault_page, 4096) == 0);
  mode = RECOVER_OPCODE;
  calls = 0;
  uintptr_t result;
#if __SIZEOF_POINTER__ == 8
  uint64_t before[2] = {0x123456789abcdef0ull, 0xfedcba9876543210ull}, after[2];
  __asm__ volatile("movdqu %2, %%xmm0; ud2; movdqu %%xmm0, %1"
                   : "=a"(result), "=m"(after) : "m"(before) : "xmm0", "memory");
  CHECK(memcmp(before, after, sizeof(before)) == 0);
#else
  double after;
  __asm__ volatile("fld1; ud2; fstpl %1"
                   : "=a"(result), "=m"(after) :: "memory");
  CHECK(after == 1.0);
#endif
  CHECK(result == 0x12345678 && calls == 1 && handler_errors == 0);
  calls = 0;
  __asm__ volatile("xor %%ecx, %%ecx; xor %%edx, %%edx; mov $1, %%eax; div %%ecx"
                   : "=a"(result) :: "ecx", "edx", "cc", "memory");
  CHECK(result == 0x12345678 && calls == 1 && handler_errors == 0);
  calls = 0;
  __asm__ volatile("int3" ::: "memory");
  CHECK(calls == 1 && handler_errors == 0);
  stack_t current, disabled = {.ss_flags = SS_DISABLE};
  CHECK(sigaltstack(NULL, &current) == 0 && current.ss_flags == 0);
  CHECK(sigaltstack(&disabled, NULL) == 0);
  CHECK(munmap(alternate.ss_sp, alternate.ss_size) == 0);
  alternate = (stack_t){0};
}

static void mask_tests(void) {
  calls = depth = 0;
  mode = NESTED;
  install(SIGUSR1, SA_NODEFER);
  CHECK(raise(SIGUSR1) == 0 && calls == 2);
  mode = RESET;
  install(SIGUSR1, 0);
  sigset_t mask, pending, original;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  CHECK(sigprocmask(SIG_BLOCK, &mask, &original) == 0);
  calls = 0;
  CHECK(raise(SIGUSR1) == 0 && raise(SIGUSR1) == 0 && calls == 0);
  CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGUSR1) == 1);
  CHECK(sigprocmask(SIG_SETMASK, &original, NULL) == 0 && calls == 1);
  CHECK(sigpending(&pending) == 0 && !sigismember(&pending, SIGUSR1));
  CHECK(signal(SIGUSR1, SIG_IGN) == (struct sigaction){.sa_sigaction = handler}.sa_handler);
  CHECK(raise(SIGUSR1) == 0 && calls == 1);
  install(SIGUSR1, 0);
  CHECK(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
  CHECK(raise(SIGUSR1) == 0);
  CHECK(signal(SIGUSR1, SIG_IGN) == (struct sigaction){.sa_sigaction = handler}.sa_handler);
  CHECK(sigpending(&pending) == 0 && !sigismember(&pending, SIGUSR1));
  CHECK(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0 && calls == 1);
  install(SIGUSR1, 0);
  CHECK(sigaction(0, NULL, NULL) == -1 && errno == EINVAL);
  CHECK(sigaction(SIGKILL, NULL, NULL) == -1 && errno == EINVAL);
  CHECK(sigaction(SIGUSR1, (void *)1, NULL) == -1 && errno == EFAULT);
  struct sigaction invalid = {.sa_handler = (sighandler_t)1, .sa_flags = 0x4000};
  CHECK(sigaction(SIGUSR1, &invalid, NULL) == -1 && errno == EINVAL);
  stack_t tiny = {.ss_sp = &invalid, .ss_size = 32};
  CHECK(sigaltstack(&tiny, NULL) == -1 && errno == ENOMEM);
  CHECK(pthread_sigmask(42, &mask, NULL) == EINVAL);
  CHECK(raise(NSIG) == -1 && errno == EINVAL);
}

static void *worker(void *unused) {
  (void)unused;
  sigset_t mask;
  pthread_sigmask(SIG_SETMASK, NULL, &mask);
  int bad = !sigismember(&mask, SIGUSR1);
  stack_t stack;
  bad |= sigaltstack(NULL, &stack) != 0 || stack.ss_flags != SS_DISABLE;
  __atomic_store_n(&worker_ready, 1, __ATOMIC_RELEASE);
  os_futex_wake(&worker_ready, 1);
  while (!__atomic_load_n(&worker_go, __ATOMIC_ACQUIRE))
    os_futex_wait(&worker_go, 0, UINT64_MAX);
  sigset_t pending;
  bad |= sigpending(&pending) != 0 || !sigismember(&pending, SIGUSR1) || calls;
  bad |= pthread_sigmask(SIG_UNBLOCK, &mask, NULL) != 0 || calls != 1;
  __atomic_store_n(&worker_ready, 2, __ATOMIC_RELEASE);
  os_futex_wake(&worker_ready, 1);
  while (calls == 1) {
    int result = os_futex_wait(&worker_wait, 0, UINT64_MAX);
    bad |= result != FUTEX_INTERRUPTED;
  }
  bad |= calls != 2 || handler_errors;
  return (void *)(uintptr_t)bad;
}

static void thread_tests(void) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  CHECK(pthread_sigmask(SIG_BLOCK, &mask, NULL) == 0);
  pthread_t thread;
  CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
  CHECK(pthread_sigmask(SIG_UNBLOCK, &mask, NULL) == 0);
  while (!__atomic_load_n(&worker_ready, __ATOMIC_ACQUIRE))
    os_futex_wait(&worker_ready, 0, UINT64_MAX);
  CHECK(pthread_kill(thread, 0) == 0);
  CHECK(pthread_kill(thread, SIGUSR1) == 0);
  __atomic_store_n(&worker_go, 1, __ATOMIC_RELEASE);
  os_futex_wake(&worker_go, 1);
  while (__atomic_load_n(&worker_ready, __ATOMIC_ACQUIRE) != 2)
    os_futex_wait(&worker_ready, 1, UINT64_MAX);
  CHECK(pthread_kill(thread, SIGUSR1) == 0);
  void *result = (void *)1;
  CHECK(pthread_join(thread, &result) == 0 && result == NULL);
}

static void fatal_tests(void) {
  for (int test = 0; test < 6; test++) {
    int child = fork();
    CHECK(child >= 0);
    if (!child) {
      if (test == 0 || test == 1) {
        mode = test == 0 ? BAD_STACK : BAD_SIMD;
#if __SIZEOF_POINTER__ == 4
        mode = BAD_STACK;
#endif
        __asm__ volatile("ud2" ::: "memory");
      } else if (test == 2) {
        install(SIGUSR1, SA_RESETHAND);
        raise(SIGUSR1);
        raise(SIGUSR1);
      } else if (test == 3) {
        sigset_t mask = SIGMASK(SIGILL);
        sigprocmask(SIG_BLOCK, &mask, NULL);
        __asm__ volatile("ud2" ::: "memory");
      } else if (test == 5) {
#if __SIZEOF_POINTER__ == 8
        __asm__ volatile("syscall" :: "a"(0x65), "D"(0) : "rcx", "r11", "memory");
#else
        __asm__ volatile("int $0x36" :: "a"(0x65), "b"(0) : "memory");
#endif
      } else {
        /* A signal cannot write its frame into an unmapped alternate stack. */
        stack_t stack = {.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
                         .ss_size = SIGSTKSZ};
        sigaltstack(&stack, NULL);
        munmap(stack.ss_sp, stack.ss_size);
        __asm__ volatile("ud2" ::: "memory");
      }
      exit(99);
    }
    if (child > 0)
      CHECK(waittid(child) == -1);
  }
}

int signal_tests(void) {
  context_tests();
  mask_tests();
  thread_tests();
  fatal_tests();
  CHECK(handler_errors == 0);
  logkf("SIGNALTEST %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
  return failures;
}
