#ifndef PLANT_FUTEX_H
#define PLANT_FUTEX_H

#include <ctypes.h>

enum { SYSCALL_FUTEX = 0x67 };
enum { FUTEX_WAIT, FUTEX_WAKE, FUTEX_OPERATION_COUNT };
enum {
  FUTEX_OK = 0,
  FUTEX_INTERRUPTED = -4,
  FUTEX_CHANGED = -11,
  FUTEX_FAULT = -14,
  FUTEX_INVALID = -22,
  FUTEX_TIMED_OUT = -110,
};

typedef struct {
  uint64_t deadline_ns;
  uintptr_t address;
  uint32_t value;
} futex_request_t;

#ifdef __cplusplus
static_assert(sizeof(futex_request_t) == (sizeof(uintptr_t) == 8 ? 24 : 16),
              "futex request ABI size");
extern "C" {
#else
_Static_assert(sizeof(futex_request_t) == (sizeof(uintptr_t) == 8 ? 24 : 16),
               "futex request ABI size");
#endif

/* Process-private, naturally aligned 32-bit words. Wait atomically compares
 * expected and sleeps; a changed value returns FUTEX_CHANGED. deadline_ns is
 * absolute monotonic time, UINT64_MAX means no deadline. Wake returns the
 * number of waiters woken (at most count). Errors are negative FUTEX_* values,
 * independent of errno. Wait may be interrupted; callers must recheck their
 * predicate with atomics. The word must remain mapped while in use. */
int os_futex_wait(uint32_t *address, uint32_t expected, uint64_t deadline_ns);
int os_futex_wake(uint32_t *address, int count);

#ifdef __cplusplus
}
#endif

#endif
