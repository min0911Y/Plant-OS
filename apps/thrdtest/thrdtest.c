#include <errno.h>
#include <futex.h>
#include <ipc.h>
#include <native_thread.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <time.h>
#include <vm.h>

enum { WORKERS = 4, ROUNDS = 40 };
static _Thread_local unsigned local_value = 17;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t barrier;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t key;
static unsigned failures, initialized, destructor_calls, shared;
static uintptr_t thread_pointers[WORKERS];
uint64_t *tls_library_value(void);
int tls_library_zero(void);

static void check(int valid, const char *name) {
  if (!valid) {
    __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED);
    logkf("THRDTEST FAIL: %s\n", name);
  }
}
static void initialize(void) { initialized++; }
static void destroy_value(void *value) {
  __atomic_fetch_add(&destructor_calls, 1, __ATOMIC_RELAXED);
  if ((uintptr_t)value > 1)
    pthread_setspecific(key, (void *)((uintptr_t)value - 1));
}

static void *worker(void *argument) {
  unsigned index = (uintptr_t)argument;
  check(local_value == 17, "main module TLS template");
  check(*tls_library_value() == 41 && !((uintptr_t)tls_library_value() & 255) &&
            tls_library_zero(),
        "shared library TLS template/alignment/zero");
  local_value = index + 100;
  *tls_library_value() = index + 200;
  errno = index + 300;
  thread_pointers[index] = native_thread_pointer();
  check(pthread_setspecific(key, (void *)2) == 0, "set thread-specific value");
  pthread_once(&once, initialize);
  for (unsigned i = 0; i < ROUNDS; i++) {
    pthread_barrier_wait(&barrier);
    pthread_mutex_lock(&mutex);
    shared++;
    pthread_mutex_unlock(&mutex);
    check(local_value == index + 100 && *tls_library_value() == index + 200 &&
              errno == (int)index + 300 &&
              pthread_getspecific(key) == (void *)2,
          "TLS/errno isolation across scheduling");
  }
  return (void *)(uintptr_t)(index + 42);
}

static void test_threads(void) {
  pthread_t threads[WORKERS];
  check(pthread_key_create(&key, destroy_value) == 0, "create TLS key");
  check(pthread_barrier_init(&barrier, NULL, WORKERS + 1) == 0,
        "initialize barrier");
  for (unsigned i = 0; i < WORKERS; i++) {
    int result =
        pthread_create(&threads[i], NULL, worker, (void *)(uintptr_t)i);
    check(result == 0, "pthread_create");
    if (result)
      exit(1);
  }
  for (unsigned i = 0; i < ROUNDS; i++)
    pthread_barrier_wait(&barrier);
  for (unsigned i = 0; i < WORKERS; i++) {
    void *value = NULL;
    check(pthread_join(threads[i], &value) == 0 &&
              value == (void *)(uintptr_t)(i + 42),
          "join and return value");
    uintptr_t page = thread_pointers[i] & ~(uintptr_t)(VM_PAGE_SIZE - 1);
    void *mapping = vm_map((void *)page, VM_PAGE_SIZE);
    check(mapping == (void *)page, "thread exit releases TLS mapping");
    if (mapping)
      vm_unmap(mapping, VM_PAGE_SIZE);
  }
  check(shared == WORKERS * ROUNDS && initialized == 1, "mutex/barrier/once");
  check(destructor_calls == 2 * WORKERS, "TSS destructor iterations");
  check(local_value == 17 && *tls_library_value() == 41 && errno == 700,
        "main thread TLS preserved");
  check(pthread_key_delete(key) == 0, "delete TLS key");
  pthread_barrier_destroy(&barrier);
}

static void test_synchronization(void) {
  pthread_mutexattr_t attributes;
  pthread_mutexattr_init(&attributes);
  pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_t recursive;
  pthread_mutex_init(&recursive, &attributes);
  check(!pthread_mutex_lock(&recursive) && !pthread_mutex_lock(&recursive) &&
            !pthread_mutex_trylock(&recursive),
        "recursive mutex");
  check(!pthread_mutex_unlock(&recursive) &&
            !pthread_mutex_unlock(&recursive) &&
            !pthread_mutex_unlock(&recursive) &&
            !pthread_mutex_destroy(&recursive),
        "recursive unlock");
  pthread_condattr_t condition_attributes;
  pthread_condattr_init(&condition_attributes);
  pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC);
  pthread_cond_t condition;
  pthread_cond_init(&condition, &condition_attributes);
  pthread_mutex_lock(&mutex);
  struct timespec deadline;
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_nsec += 20000000;
  if (deadline.tv_nsec >= 1000000000) {
    deadline.tv_nsec -= 1000000000;
    deadline.tv_sec++;
  }
  check(pthread_cond_timedwait(&condition, &mutex, &deadline) == ETIMEDOUT,
        "monotonic condition timeout");
  check(pthread_mutex_trylock(&mutex) == EBUSY, "condition reacquires mutex");
  pthread_mutex_unlock(&mutex);
  pthread_cond_destroy(&condition);
  pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
  check(!pthread_rwlock_rdlock(&rw) && !pthread_rwlock_tryrdlock(&rw) &&
            pthread_rwlock_trywrlock(&rw) == EBUSY,
        "rwlock readers");
  pthread_rwlock_unlock(&rw);
  pthread_rwlock_unlock(&rw);
  check(!pthread_rwlock_wrlock(&rw) && pthread_rwlock_tryrdlock(&rw) == EBUSY,
        "rwlock writer");
  pthread_rwlock_unlock(&rw);
  pthread_rwlock_destroy(&rw);
}

static void *detached_worker(void *argument) {
  unsigned *finished = argument;
  __atomic_fetch_add(finished, 1, __ATOMIC_RELEASE);
  return NULL;
}
static void test_detach(void) {
  unsigned finished = 0;
  for (unsigned i = 0; i < 12; i++) {
    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, i & 1 ? PTHREAD_CREATE_DETACHED
                                                   : PTHREAD_CREATE_JOINABLE);
    int result =
        pthread_create(&thread, &attributes, detached_worker, &finished);
    check(result == 0, "create detachable thread");
    if (!result && !(i & 1))
      check(pthread_detach(thread) == 0, "detach existing thread");
  }
  check(native_thread_call(THREAD_WAIT_GROUP, NULL) == 0 && finished == 12,
        "detached threads finish and release resources");
}

static void mapping_worker(uintptr_t argument) {
  uint32_t *ready = (void *)argument;
  while (!__atomic_load_n(ready, __ATOMIC_ACQUIRE))
    os_futex_wait(ready, 0, UINT64_MAX);
  _exit(0);
}

static void test_thread_mappings(void) {
  char *stack = vm_map(NULL, 3 * VM_PAGE_SIZE);
  check(stack != NULL, "managed stack allocation");
  if (!stack)
    return;
  uint32_t ready = 0;
  native_thread_request_t thread = {
      .entry = (uintptr_t)mapping_worker,
      .stack_top = (uintptr_t)stack + 3 * VM_PAGE_SIZE,
      .argument = (uintptr_t)&ready,
      .stack = {(uintptr_t)stack, 3 * VM_PAGE_SIZE},
      .flags = THREAD_JOINABLE,
  };
  if (native_thread_call(THREAD_CREATE, &thread)) {
    check(false, "raw managed thread creation");
    vm_unmap(stack, 3 * VM_PAGE_SIZE);
    return;
  }
  check(vm_unmap(stack + VM_PAGE_SIZE, VM_PAGE_SIZE) == 0,
        "partial managed stack unmap");
  __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
  os_futex_wake(&ready, 1);
  check(native_thread_call(THREAD_JOIN, &thread) == 0,
        "join raw managed thread");
  void *reused = vm_map(stack, 3 * VM_PAGE_SIZE);
  check(reused == stack, "remaining managed pages reclaimed");
  if (reused) {
    vm_unmap(reused, 3 * VM_PAGE_SIZE);
  } else {
    vm_unmap(stack, VM_PAGE_SIZE);
    vm_unmap(stack + 2 * VM_PAGE_SIZE, VM_PAGE_SIZE);
  }
}

int test_smp_vm(void);

int main(void) {
  errno = 700;
  check(native_thread_pointer() != 0 && tls_library_zero(), "initial TLS");
  test_threads();
  test_synchronization();
  test_detach();
  test_thread_mappings();
  check(test_smp_vm(), "cross-CPU translation and protection");
  int child = fork();
  if (!child) {
    if (local_value != 17 || *tls_library_value() != 41)
      _exit(1);
    local_value = 29;
    *tls_library_value() = 30;
    _exit(0);
  }
  check(child > 0 && waittid(child) == 0 && local_value == 17 &&
            *tls_library_value() == 41,
        "fork TLS copy-on-write");
  if (!failures) {
    puts("THRDTEST PASS");
    logk("THRDTEST PASS\n");
  }
  return failures != 0;
}
