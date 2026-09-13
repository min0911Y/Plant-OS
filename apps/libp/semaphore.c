#include "time_internal.h"

#include <errno.h>
#include <futex.h>
#include <semaphore.h>

static int sem_take(sem_t *semaphore) {
  uint32_t value = __atomic_load_n(&semaphore->value, __ATOMIC_RELAXED);
  while (value) {
    if (__atomic_compare_exchange_n(&semaphore->value, &value, value - 1,
                                    1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return 1;
  }
  return 0;
}

int sem_init(sem_t *semaphore, int shared, unsigned value) {
  if (shared || value > SEM_VALUE_MAX) {
    errno = shared ? ENOTSUP : EINVAL;
    return -1;
  }
  __atomic_store_n(&semaphore->value, value, __ATOMIC_RELAXED);
  return 0;
}

int sem_destroy(sem_t *semaphore) {
  (void)semaphore;
  return 0;
}

int sem_post(sem_t *semaphore) {
  uint32_t value = __atomic_load_n(&semaphore->value, __ATOMIC_RELAXED);
  do {
    if (value == SEM_VALUE_MAX) {
      errno = EOVERFLOW;
      return -1;
    }
  } while (!__atomic_compare_exchange_n(&semaphore->value, &value, value + 1,
                                         1, __ATOMIC_RELEASE,
                                         __ATOMIC_RELAXED));
  if (!value)
    os_futex_wake(&semaphore->value, 1);
  return 0;
}

int sem_wait(sem_t *semaphore) {
  while (!sem_take(semaphore)) {
    int result = os_futex_wait(&semaphore->value, 0, UINT64_MAX);
    if (result == FUTEX_INTERRUPTED) {
      errno = EINTR;
      return -1;
    }
    if (result != FUTEX_OK && result != FUTEX_CHANGED) {
      errno = EINVAL;
      return -1;
    }
  }
  return 0;
}

int sem_trywait(sem_t *semaphore) {
  if (sem_take(semaphore))
    return 0;
  errno = EAGAIN;
  return -1;
}

int sem_timedwait(sem_t *semaphore, const struct timespec *time) {
  if (sem_take(semaphore))
    return 0;

  uint64_t deadline;
  int error = runtime_deadline(CLOCK_REALTIME, time, &deadline);
  if (error) {
    errno = error;
    return -1;
  }
  for (;;) {
    int result = os_futex_wait(&semaphore->value, 0, deadline);
    if (result == FUTEX_TIMED_OUT) {
      errno = ETIMEDOUT;
      return -1;
    }
    if (result == FUTEX_INTERRUPTED) {
      errno = EINTR;
      return -1;
    }
    if (result != FUTEX_OK && result != FUTEX_CHANGED) {
      errno = EINVAL;
      return -1;
    }
    if (sem_take(semaphore))
      return 0;
  }
}

int sem_getvalue(sem_t *semaphore, int *value) {
  *value = __atomic_load_n(&semaphore->value, __ATOMIC_RELAXED);
  return 0;
}
