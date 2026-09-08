#include <futex.h>

int futex_syscall(unsigned operation, const futex_request_t *request,
                  size_t size);

int os_futex_wait(uint32_t *address, uint32_t expected, uint64_t deadline_ns) {
  futex_request_t request = {deadline_ns, (uintptr_t)address, expected};
  return futex_syscall(FUTEX_WAIT, &request, sizeof(request));
}

int os_futex_wake(uint32_t *address, int count) {
  if (count < 0)
    return FUTEX_INVALID;
  futex_request_t request = {0, (uintptr_t)address, (uint32_t)count};
  return futex_syscall(FUTEX_WAKE, &request, sizeof(request));
}
