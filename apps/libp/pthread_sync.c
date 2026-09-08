#include <errno.h>
#include <futex.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <syscall.h>

int runtime_deadline(clockid_t clock, const struct timespec *time,
                     uint64_t *deadline);

int pthread_mutexattr_init(pthread_mutexattr_t *attributes) {
  *attributes = (pthread_mutexattr_t){PTHREAD_MUTEX_NORMAL};
  return 0;
}
int pthread_mutexattr_destroy(pthread_mutexattr_t *attributes) { return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *attributes, int type) {
  if (type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK)
    return EINVAL;
  attributes->type = type;
  return 0;
}
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attributes, int shared) {
  return shared == PTHREAD_PROCESS_PRIVATE ? 0 : ENOTSUP;
}
int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attributes) {
  *mutex = (pthread_mutex_t){.type = attributes ? attributes->type
                                                : PTHREAD_MUTEX_NORMAL};
  return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  return __atomic_load_n(&mutex->state, __ATOMIC_RELAXED) ? EBUSY : 0;
}

static int mutex_acquire(pthread_mutex_t *mutex, bool try, uint64_t deadline) {
  uint32_t owner = mutex->type == PTHREAD_MUTEX_NORMAL ? 0 : NowTaskID();
  if (owner && __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == owner) {
    if (mutex->type != PTHREAD_MUTEX_RECURSIVE)
      return try ? EBUSY : EDEADLK;
    if (mutex->depth == UINT_MAX)
      return EAGAIN;
    mutex->depth++;
    return 0;
  }
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&mutex->state, &expected, 1, false,
                                   __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
    if (try)
      return EBUSY;
    while (__atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE)) {
      int result = os_futex_wait(&mutex->state, 2, deadline);
      if (result == FUTEX_TIMED_OUT)
        return ETIMEDOUT;
      if (result != FUTEX_OK && result != FUTEX_CHANGED &&
          result != FUTEX_INTERRUPTED)
        return EINVAL;
    }
  }
  if (owner) {
    __atomic_store_n(&mutex->owner, owner, __ATOMIC_RELAXED);
    mutex->depth = 1;
  }
  return 0;
}
int pthread_mutex_lock(pthread_mutex_t *mutex) {
  return mutex_acquire(mutex, false, UINT64_MAX);
}
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  return mutex_acquire(mutex, true, 0);
}
int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                            const struct timespec *time) {
  uint64_t deadline;
  int error = runtime_deadline(CLOCK_REALTIME, time, &deadline);
  return error ? error : mutex_acquire(mutex, false, deadline);
}
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  if (mutex->type != PTHREAD_MUTEX_NORMAL) {
    if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) !=
        (unsigned)NowTaskID())
      return EPERM;
    if (--mutex->depth)
      return 0;
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELAXED);
  }
  if (__atomic_exchange_n(&mutex->state, 0, __ATOMIC_RELEASE) == 2)
    os_futex_wake(&mutex->state, 1);
  return 0;
}

typedef struct pthread_waiter {
  struct pthread_waiter *previous, *next;
  uint32_t notified;
} pthread_waiter_t;

int pthread_condattr_init(pthread_condattr_t *attributes) {
  attributes->clock = CLOCK_REALTIME;
  return 0;
}
int pthread_condattr_destroy(pthread_condattr_t *attributes) { return 0; }
int pthread_condattr_setclock(pthread_condattr_t *attributes, clockid_t clock) {
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
    return EINVAL;
  attributes->clock = clock;
  return 0;
}
int pthread_condattr_getclock(const pthread_condattr_t *attributes,
                              clockid_t *clock) {
  *clock = attributes->clock;
  return 0;
}
int pthread_condattr_setpshared(pthread_condattr_t *attributes, int shared) {
  return shared == PTHREAD_PROCESS_PRIVATE ? 0 : ENOTSUP;
}
int pthread_cond_init(pthread_cond_t *condition,
                      const pthread_condattr_t *attributes) {
  *condition = (pthread_cond_t){.clock = attributes ? attributes->clock
                                                    : CLOCK_REALTIME};
  return 0;
}
int pthread_cond_destroy(pthread_cond_t *condition) {
  pthread_mutex_lock(&condition->lock);
  int result = condition->first ? EBUSY : 0;
  pthread_mutex_unlock(&condition->lock);
  return result;
}

static void condition_remove(pthread_cond_t *condition,
                             pthread_waiter_t *waiter) {
  if (waiter->previous)
    waiter->previous->next = waiter->next;
  else
    condition->first = waiter->next;
  if (waiter->next)
    waiter->next->previous = waiter->previous;
  else
    condition->last = waiter->previous;
}

static int condition_wait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                          uint64_t deadline) {
  pthread_waiter_t waiter = {0};
  pthread_mutex_lock(&condition->lock);
  waiter.previous = condition->last;
  if (condition->last)
    condition->last->next = &waiter;
  else
    condition->first = &waiter;
  condition->last = &waiter;
  int error = pthread_mutex_unlock(mutex);
  if (error)
    condition_remove(condition, &waiter);
  pthread_mutex_unlock(&condition->lock);
  if (error)
    return error;
  while (!__atomic_load_n(&waiter.notified, __ATOMIC_ACQUIRE)) {
    int result = os_futex_wait(&waiter.notified, 0, deadline);
    if (result == FUTEX_TIMED_OUT) {
      error = ETIMEDOUT;
      break;
    }
    if (result != FUTEX_OK && result != FUTEX_CHANGED &&
        result != FUTEX_INTERRUPTED) {
      error = EINVAL;
      break;
    }
  }
  pthread_mutex_lock(&condition->lock);
  if (!waiter.notified)
    condition_remove(condition, &waiter);
  else
    error = 0;
  pthread_mutex_unlock(&condition->lock);
  int locked = pthread_mutex_lock(mutex);
  return locked ? locked : error;
}
int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex) {
  return condition_wait(condition, mutex, UINT64_MAX);
}
int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *time) {
  uint64_t deadline;
  int error = runtime_deadline(condition->clock, time, &deadline);
  return error ? error : condition_wait(condition, mutex, deadline);
}
static int condition_signal(pthread_cond_t *condition, bool all) {
  pthread_mutex_lock(&condition->lock);
  while (condition->first) {
    pthread_waiter_t *waiter = condition->first;
    condition_remove(condition, waiter);
    __atomic_store_n(&waiter->notified, 1, __ATOMIC_RELEASE);
    os_futex_wake(&waiter->notified, 1);
    if (!all)
      break;
  }
  pthread_mutex_unlock(&condition->lock);
  return 0;
}
int pthread_cond_signal(pthread_cond_t *condition) {
  return condition_signal(condition, false);
}
int pthread_cond_broadcast(pthread_cond_t *condition) {
  return condition_signal(condition, true);
}

int pthread_once(pthread_once_t *once, void (*initialize)(void)) {
  if (__atomic_load_n(once, __ATOMIC_ACQUIRE) == 2)
    return 0;
  uint32_t expected = 0;
  if (__atomic_compare_exchange_n(once, &expected, 1, false, __ATOMIC_ACQUIRE,
                                  __ATOMIC_RELAXED)) {
    initialize();
    __atomic_store_n(once, 2, __ATOMIC_RELEASE);
    os_futex_wake(once, INT_MAX);
  } else {
    while (__atomic_load_n(once, __ATOMIC_ACQUIRE) != 2)
      os_futex_wait(once, 1, UINT64_MAX);
  }
  return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *lock,
                        const pthread_rwlockattr_t *attributes) {
  *lock = (pthread_rwlock_t){0};
  return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
  return lock->readers || lock->writing || lock->writers ? EBUSY : 0;
}
static int rwlock_acquire(pthread_rwlock_t *lock, bool write, bool try) {
  int result = try ? pthread_mutex_trylock(&lock->lock)
                   : pthread_mutex_lock(&lock->lock);
  if (result)
    return result;
  if (write)
    lock->writers++;
  while (lock->writing || (write ? lock->readers : lock->writers)) {
    if (try) {
      result = EBUSY;
      break;
    }
    pthread_cond_wait(&lock->changed, &lock->lock);
  }
  if (write)
    lock->writers--;
  if (!result) {
    if (write)
      lock->writing = 1;
    else
      lock->readers++;
  }
  pthread_mutex_unlock(&lock->lock);
  return result;
}
int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
  return rwlock_acquire(lock, false, false);
}
int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
  return rwlock_acquire(lock, true, false);
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock) {
  return rwlock_acquire(lock, false, true);
}
int pthread_rwlock_trywrlock(pthread_rwlock_t *lock) {
  return rwlock_acquire(lock, true, true);
}
int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
  pthread_mutex_lock(&lock->lock);
  int result = 0;
  if (lock->writing)
    lock->writing = 0;
  else if (lock->readers)
    lock->readers--;
  else
    result = EPERM;
  pthread_cond_broadcast(&lock->changed);
  pthread_mutex_unlock(&lock->lock);
  return result;
}

int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attributes,
                         unsigned count) {
  if (!count)
    return EINVAL;
  *barrier = (pthread_barrier_t){.count = count};
  return 0;
}
int pthread_barrier_destroy(pthread_barrier_t *barrier) {
  return barrier->arrived ? EBUSY : 0;
}
int pthread_barrier_wait(pthread_barrier_t *barrier) {
  pthread_mutex_lock(&barrier->lock);
  uint64_t generation = barrier->generation;
  int result = 0;
  if (++barrier->arrived == barrier->count) {
    barrier->arrived = 0;
    barrier->generation++;
    pthread_cond_broadcast(&barrier->changed);
    result = PTHREAD_BARRIER_SERIAL_THREAD;
  } else {
    while (generation == barrier->generation)
      pthread_cond_wait(&barrier->changed, &barrier->lock);
  }
  pthread_mutex_unlock(&barrier->lock);
  return result;
}
