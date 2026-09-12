#ifndef PLANT_PTHREAD_H
#define PLANT_PTHREAD_H

#include <stdint.h>
#include <signal.h>
#include <time.h>

typedef struct __pthread *pthread_t;
typedef uint64_t pthread_key_t;
typedef uint32_t pthread_once_t;
typedef struct {
  uint32_t state, owner;
  unsigned depth;
  int type;
} pthread_mutex_t;
typedef struct {
  int type;
} pthread_mutexattr_t;
typedef struct {
  uint32_t sequence, waiters;
  clockid_t clock;
} pthread_cond_t;
typedef struct {
  clockid_t clock;
} pthread_condattr_t;
typedef struct {
  size_t stack_size, guard_size;
  void *stack_address;
  int detached;
} pthread_attr_t;
typedef struct {
  pthread_mutex_t lock;
  pthread_cond_t changed;
  unsigned readers, writers;
  int writing;
} pthread_rwlock_t;
typedef struct {
  int unused;
} pthread_rwlockattr_t;
typedef struct {
  pthread_mutex_t lock;
  unsigned count, arrived;
  uint32_t generation, users;
} pthread_barrier_t;
typedef struct {
  int unused;
} pthread_barrierattr_t;

#define PTHREAD_MUTEX_INITIALIZER                                              \
  { 0 }
#define PTHREAD_COND_INITIALIZER                                               \
  { 0 }
#define PTHREAD_RWLOCK_INITIALIZER                                             \
  { 0 }
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_STACK_MIN 16384
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

#ifdef __cplusplus
extern "C" {
#endif
int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                   void *(*entry)(void *), void *argument);
pthread_t pthread_self(void);
int pthread_equal(pthread_t left, pthread_t right);
int pthread_kill(pthread_t thread, int sig);
void pthread_exit(void *result) __attribute__((noreturn));
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);
int pthread_attr_init(pthread_attr_t *attributes);
int pthread_attr_destroy(pthread_attr_t *attributes);
int pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t *attributes, size_t *size);
int pthread_attr_setstack(pthread_attr_t *attributes, void *address,
                          size_t size);
int pthread_attr_getstack(const pthread_attr_t *attributes, void **address,
                          size_t *size);
int pthread_attr_setdetachstate(pthread_attr_t *attributes, int state);
int pthread_attr_getdetachstate(const pthread_attr_t *attributes, int *state);
int pthread_attr_setguardsize(pthread_attr_t *attributes, size_t size);
int pthread_attr_getguardsize(const pthread_attr_t *attributes, size_t *size);
int pthread_mutexattr_init(pthread_mutexattr_t *attributes);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attributes);
int pthread_mutexattr_settype(pthread_mutexattr_t *attributes, int type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attributes, int shared);
int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attributes);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                            const struct timespec *deadline);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_condattr_init(pthread_condattr_t *attributes);
int pthread_condattr_destroy(pthread_condattr_t *attributes);
int pthread_condattr_setclock(pthread_condattr_t *attributes, clockid_t clock);
int pthread_condattr_getclock(const pthread_condattr_t *attributes,
                              clockid_t *clock);
int pthread_condattr_setpshared(pthread_condattr_t *attributes, int shared);
int pthread_cond_init(pthread_cond_t *condition,
                      const pthread_condattr_t *attributes);
int pthread_cond_destroy(pthread_cond_t *condition);
int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *deadline);
int pthread_cond_signal(pthread_cond_t *condition);
int pthread_cond_broadcast(pthread_cond_t *condition);
int pthread_once(pthread_once_t *once, void (*initialize)(void));
int pthread_key_create(pthread_key_t *key, void (*destroy)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
int pthread_rwlock_init(pthread_rwlock_t *lock,
                        const pthread_rwlockattr_t *attributes);
int pthread_rwlock_destroy(pthread_rwlock_t *lock);
int pthread_rwlock_rdlock(pthread_rwlock_t *lock);
int pthread_rwlock_wrlock(pthread_rwlock_t *lock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *lock);
int pthread_rwlock_unlock(pthread_rwlock_t *lock);
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attributes,
                         unsigned count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);
#ifdef __cplusplus
}
#endif
#endif
