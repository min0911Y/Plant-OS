#include "runtime_lifecycle.h"
#include <errno.h>
#include <fenv.h>
#include <futex.h>
#include <ipc.h>
#include <limits.h>
#include <native_thread.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <tls.h>
#include <vm.h>

enum {
  THREAD_DETACHED = 1,
  THREAD_FINISHED = 2,
  THREAD_JOINING = 4,
  THREAD_EXTERNAL = 8,
  THREAD_MAIN = 16
};
struct __pthread {
  uint32_t tid, generation, state, published;
  uintptr_t entry, argument;
  void *result;
};
typedef struct {
  pthread_key_t key;
  void *value;
} thread_value_t;
typedef struct thread_destructor {
  struct thread_destructor *next;
  void (*function)(void *);
  void *argument;
} thread_destructor_t;
typedef struct {
  struct __pthread local;
  pthread_t handle;
  thread_value_t *values;
  size_t value_count;
  thread_destructor_t *destructors;
  fenv_t initial_environment;
} thread_runtime_t;
typedef struct {
  uint32_t generation;
  bool used;
  void (*destroy)(void *);
} thread_key_t;

static thread_key_t *keys;
static size_t key_count;
static pthread_mutex_t keys_lock = PTHREAD_MUTEX_INITIALIZER;
static int bootstrap_errno;
extern const runtime_linker_t *runtime_linker;
void api_exit(unsigned status) __attribute__((noreturn));
int api_fork(void);

int *__errno_location(void) {
  tls_control_t *tls = (void *)native_thread_pointer();
  return tls ? &tls->error_number : &bootstrap_errno;
}

void *__tls_get_addr(const tls_index_t *index) {
  tls_control_t *tls = tls_current();
  if (!index->module || index->module > tls->module_count)
    abort();
  return (char *)tls->modules[index->module] + index->offset;
}
#if __SIZEOF_POINTER__ == 4
__attribute__((regparm(1))) void *___tls_get_addr(const tls_index_t *index) {
  return __tls_get_addr(index);
}
#endif

static tls_control_t *thread_tls_allocate(thread_region_t *region) {
  tls_control_t *tls;
  if (runtime_linker) {
    tls = runtime_linker->tls_allocate(sizeof(thread_runtime_t), region);
  } else {
    size_t size = (sizeof(*tls) + sizeof(thread_runtime_t) + VM_PAGE_SIZE - 1) &
                  ~(size_t)(VM_PAGE_SIZE - 1);
    tls = vm_map(NULL, size);
    if (tls) {
      tls->self = tls;
      *region = (thread_region_t){(uintptr_t)tls, size};
    }
  }
  if (tls) {
    tls_control_t *parent = (void *)native_thread_pointer();
    if (parent)
      tls->locale = parent->locale;
    tls->runtime = tls + 1;
    thread_runtime_t *runtime = tls->runtime;
    runtime->handle = &runtime->local;
  }
  return tls;
}

int runtime_thread_initialize(const runtime_linker_t *linker) {
  runtime_linker = linker;
  native_thread_request_t request = {0};
  tls_control_t *tls = thread_tls_allocate(&request.tls);
  if (!tls)
    return ENOMEM;
  thread_runtime_t *runtime = tls->runtime;
  runtime->local.tid = NowTaskID();
  runtime->local.generation = ipc_generation();
  runtime->local.state = THREAD_EXTERNAL | THREAD_MAIN;
  request.thread_pointer = (uintptr_t)tls;
  intptr_t result = native_thread_call(THREAD_SET_POINTER, &request);
  if (result) {
    vm_unmap((void *)request.tls.address, request.tls.size);
    return -result;
  }
  return 0;
}

pthread_t pthread_self(void) {
  thread_runtime_t *runtime = tls_current()->runtime;
  return runtime->handle;
}
int pthread_equal(pthread_t left, pthread_t right) { return left == right; }
int pthread_kill(pthread_t thread, int sig) {
  if (!thread || sig < 0 || sig >= NSIG)
    return EINVAL;
  return -signal_call(SIGNAL_SEND, sig, thread->tid, thread->generation);
}
int sched_yield(void) {
  api_yield();
  return 0;
}

int pthread_key_create(pthread_key_t *key, void (*destroy)(void *)) {
  if (!key)
    return EINVAL;
  pthread_mutex_lock(&keys_lock);
  size_t index = 0;
  while (index < key_count &&
         (keys[index].used || keys[index].generation == UINT32_MAX))
    index++;
  if (index == key_count) {
    size_t count = key_count ? key_count * 2 : 16;
    if (count < key_count || count > UINT32_MAX ||
        count > SIZE_MAX / sizeof(*keys)) {
      pthread_mutex_unlock(&keys_lock);
      return EAGAIN;
    }
    thread_key_t *replacement = realloc(keys, count * sizeof(*keys));
    if (!replacement) {
      pthread_mutex_unlock(&keys_lock);
      return ENOMEM;
    }
    memset(replacement + key_count, 0, (count - key_count) * sizeof(*keys));
    keys = replacement;
    key_count = count;
  }
  thread_key_t *slot = keys + index;
  slot->generation++;
  slot->used = true;
  slot->destroy = destroy;
  *key = (uint64_t)slot->generation << 32 | index;
  pthread_mutex_unlock(&keys_lock);
  return 0;
}
int pthread_key_delete(pthread_key_t key) {
  size_t index = (uint32_t)key;
  pthread_mutex_lock(&keys_lock);
  bool valid = index < key_count && keys[index].used &&
               keys[index].generation == key >> 32;
  if (valid)
    keys[index].used = false;
  pthread_mutex_unlock(&keys_lock);
  return valid ? 0 : EINVAL;
}
void *pthread_getspecific(pthread_key_t key) {
  thread_runtime_t *runtime = tls_current()->runtime;
  size_t index = (uint32_t)key;
  return index < runtime->value_count && runtime->values[index].key == key
             ? runtime->values[index].value
             : NULL;
}
int pthread_setspecific(pthread_key_t key, const void *value) {
  thread_runtime_t *runtime = tls_current()->runtime;
  size_t index = (uint32_t)key;
  pthread_mutex_lock(&keys_lock);
  bool valid = index < key_count && keys[index].used &&
               keys[index].generation == key >> 32;
  pthread_mutex_unlock(&keys_lock);
  if (!valid)
    return EINVAL;
  if (index >= runtime->value_count) {
    if (!value)
      return 0;
    size_t count = index + 1;
    if (count > SIZE_MAX / sizeof(*runtime->values))
      return ENOMEM;
    thread_value_t *values = realloc(runtime->values, count * sizeof(*values));
    if (!values)
      return ENOMEM;
    memset(values + runtime->value_count, 0,
           (count - runtime->value_count) * sizeof(*values));
    runtime->values = values;
    runtime->value_count = count;
  }
  runtime->values[index] = (thread_value_t){key, (void *)value};
  return 0;
}

void runtime_thread_finalize(void) {
  if (!native_thread_pointer())
    return;
  thread_runtime_t *runtime = tls_current()->runtime;
  while (runtime->destructors) {
    thread_destructor_t *entry = runtime->destructors;
    runtime->destructors = entry->next;
    void (*function)(void *) = entry->function;
    void *argument = entry->argument;
    free(entry);
    function(argument);
  }
  for (unsigned pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; pass++) {
    bool called = false;
    for (size_t i = 0; i < runtime->value_count; i++) {
      thread_value_t value = runtime->values[i];
      if (!value.value)
        continue;
      runtime->values[i].value = NULL;
      pthread_mutex_lock(&keys_lock);
      void (*destroy)(void *) =
          i < key_count && keys[i].used && keys[i].generation == value.key >> 32
              ? keys[i].destroy
              : NULL;
      pthread_mutex_unlock(&keys_lock);
      if (destroy) {
        destroy(value.value);
        called = true;
      }
    }
    if (!called)
      break;
  }
  free(runtime->values);
  runtime->values = NULL;
  runtime->value_count = 0;
}

int __cxa_thread_atexit_impl(void (*function)(void *), void *argument,
                             void *dso) {
  thread_runtime_t *runtime = tls_current()->runtime;
  thread_destructor_t *entry = malloc(sizeof(*entry));
  if (!entry)
    return -1;
  *entry = (thread_destructor_t){runtime->destructors, function, argument};
  runtime->destructors = entry;
  return 0;
}

void _exit(int status) {
  if (native_thread_pointer()) {
    pthread_t self = pthread_self();
    if (!(__atomic_load_n(&self->state, __ATOMIC_RELAXED) & THREAD_MAIN)) {
      runtime_thread_finalize();
      uint32_t previous =
          __atomic_fetch_or(&self->state, THREAD_FINISHED, __ATOMIC_RELEASE);
      if (!(previous & THREAD_EXTERNAL) && (previous & THREAD_DETACHED))
        free(self);
    }
  }
  api_exit(status);
}

void pthread_exit(void *result) {
  pthread_t self = pthread_self();
  self->result = result;
  if (__atomic_load_n(&self->state, __ATOMIC_RELAXED) & THREAD_MAIN) {
    runtime_thread_finalize();
    native_thread_call(THREAD_WAIT_GROUP, NULL);
  }
  _exit(0);
}

static void thread_entry(uintptr_t argument) {
  pthread_t self = pthread_self();
  while (!__atomic_load_n(&self->published, __ATOMIC_ACQUIRE))
    os_futex_wait(&self->published, 0, UINT64_MAX);
  thread_runtime_t *runtime = tls_current()->runtime;
  fesetenv(&runtime->initial_environment);
  if (__atomic_load_n(&self->state, __ATOMIC_RELAXED) & THREAD_EXTERNAL) {
    ((void (*)(uintptr_t))self->entry)(self->argument);
    _exit(0);
  }
  pthread_exit(((void *(*)(void *))self->entry)((void *)self->argument));
}

static int thread_start(native_thread_request_t *request, tls_control_t *tls,
                        pthread_t handle) {
  thread_runtime_t *runtime = tls->runtime;
  runtime->handle = handle;
  fegetenv(&runtime->initial_environment);
  request->entry = (uintptr_t)thread_entry;
  request->thread_pointer = (uintptr_t)tls;
  intptr_t result = native_thread_call(THREAD_CREATE, request);
  if (result) {
    return -result;
  }
  handle->tid = request->tid;
  handle->generation = request->generation;
  __atomic_store_n(&handle->published, 1, __ATOMIC_RELEASE);
  os_futex_wake(&handle->published, 1);
  return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                   void *(*entry)(void *), void *argument) {
  if (!thread || !entry)
    return EINVAL;
  pthread_attr_t defaults;
  if (!attributes) {
    pthread_attr_init(&defaults);
    attributes = &defaults;
  }
  pthread_t handle = calloc(1, sizeof(*handle));
  if (!handle)
    return ENOMEM;
  native_thread_request_t request = {0};
  tls_control_t *tls = thread_tls_allocate(&request.tls);
  int error = ENOMEM;
  if (!tls)
    goto failed;
  if (attributes->stack_size < PTHREAD_STACK_MIN ||
      attributes->stack_size > SIZE_MAX - VM_PAGE_SIZE) {
    error = EINVAL;
    goto failed;
  }
  size_t size =
      (attributes->stack_size + VM_PAGE_SIZE - 1) & ~(size_t)(VM_PAGE_SIZE - 1);
  uintptr_t stack = (uintptr_t)attributes->stack_address;
  if (!stack) {
    stack = (uintptr_t)vm_map(NULL, size);
    if (!stack)
      goto failed;
    request.stack = (thread_region_t){stack, size};
  } else {
    size = attributes->stack_size;
  }
  if (stack > UINTPTR_MAX - size) {
    error = EINVAL;
    goto failed;
  }
  request.stack_top = (stack + size) & ~(uintptr_t)15;
  request.flags = attributes->detached ? 0 : THREAD_JOINABLE;
  handle->entry = (uintptr_t)entry;
  handle->argument = (uintptr_t)argument;
  handle->state = attributes->detached ? THREAD_DETACHED : 0;
  error = thread_start(&request, tls, handle);
  if (!error) {
    *thread = handle;
    return 0;
  }
failed:
  if (request.stack.size)
    vm_unmap((void *)request.stack.address, request.stack.size);
  if (request.tls.size)
    vm_unmap((void *)request.tls.address, request.tls.size);
  free(handle);
  return error;
}

int AddThread(const char *name, uintptr_t entry, uintptr_t stack_top,
              uintptr_t argument) {
  native_thread_request_t request = {.stack_top = stack_top & ~(uintptr_t)15};
  tls_control_t *tls = thread_tls_allocate(&request.tls);
  if (!tls)
    return -1;
  thread_runtime_t *runtime = tls->runtime;
  runtime->local.entry = entry;
  runtime->local.argument = argument;
  runtime->local.state = THREAD_EXTERNAL;
  if (name) {
    size_t length = strlen(name);
    if (length >= sizeof(request.name))
      length = sizeof(request.name) - 1;
    memcpy(request.name, name, length);
  }
  if (!entry || thread_start(&request, tls, &runtime->local)) {
    vm_unmap((void *)request.tls.address, request.tls.size);
    return -1;
  }
  return request.tid;
}

int pthread_join(pthread_t thread, void **result) {
  if (!thread || thread == pthread_self())
    return EDEADLK;
  uint32_t state = __atomic_load_n(&thread->state, __ATOMIC_ACQUIRE);
  do {
    if (state & (THREAD_EXTERNAL | THREAD_DETACHED | THREAD_JOINING))
      return EINVAL;
  } while (!__atomic_compare_exchange_n(&thread->state, &state,
                                        state | THREAD_JOINING, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
  native_thread_request_t request = {.tid = thread->tid,
                                     .generation = thread->generation};
  if (native_thread_call(THREAD_JOIN, &request)) {
    __atomic_fetch_and(&thread->state, ~THREAD_JOINING, __ATOMIC_RELEASE);
    return EINVAL;
  }
  if (result)
    *result = thread->result;
  free(thread);
  return 0;
}
int pthread_detach(pthread_t thread) {
  if (!thread)
    return EINVAL;
  native_thread_request_t request = {.tid = thread->tid,
                                     .generation = thread->generation};
  uint32_t state = __atomic_load_n(&thread->state, __ATOMIC_ACQUIRE);
  do {
    if (state & (THREAD_EXTERNAL | THREAD_DETACHED | THREAD_JOINING))
      return EINVAL;
  } while (!__atomic_compare_exchange_n(&thread->state, &state,
                                        state | THREAD_DETACHED, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
  intptr_t result = native_thread_call(THREAD_DETACH, &request);
  if (state & THREAD_FINISHED)
    free(thread);
  return result ? EINVAL : 0;
}

int pthread_attr_init(pthread_attr_t *attributes) {
  *attributes = (pthread_attr_t){.stack_size = 2 * 1024 * 1024};
  return 0;
}
int pthread_attr_destroy(pthread_attr_t *attributes) { return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size) {
  if (size < PTHREAD_STACK_MIN || size > SIZE_MAX - VM_PAGE_SIZE)
    return EINVAL;
  attributes->stack_size = size;
  return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t *attributes, size_t *size) {
  *size = attributes->stack_size;
  return 0;
}
int pthread_attr_setstack(pthread_attr_t *attributes, void *address,
                          size_t size) {
  if (!address || (uintptr_t)address > UINTPTR_MAX - size ||
      size < PTHREAD_STACK_MIN)
    return EINVAL;
  attributes->stack_address = address;
  attributes->stack_size = size;
  return 0;
}
int pthread_attr_getstack(const pthread_attr_t *attributes, void **address,
                          size_t *size) {
  *address = attributes->stack_address;
  *size = attributes->stack_size;
  return 0;
}
int pthread_attr_setdetachstate(pthread_attr_t *attributes, int state) {
  if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
    return EINVAL;
  attributes->detached = state;
  return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t *attributes, int *state) {
  *state = attributes->detached;
  return 0;
}
int pthread_attr_setguardsize(pthread_attr_t *attributes, size_t size) {
  if (size)
    return ENOTSUP;
  attributes->guard_size = 0;
  return 0;
}
int pthread_attr_getguardsize(const pthread_attr_t *attributes, size_t *size) {
  *size = attributes->guard_size;
  return 0;
}

void runtime_thread_after_fork(void) {
  if (!native_thread_pointer())
    return;
  thread_runtime_t *runtime = tls_current()->runtime;
  runtime->local = *runtime->handle;
  runtime->local.tid = NowTaskID();
  runtime->local.generation = ipc_generation();
  runtime->local.state = THREAD_MAIN | THREAD_EXTERNAL;
  runtime->handle = &runtime->local;
}
int fork(void) {
  pthread_mutex_lock(&runtime_environment_lock);
  pthread_mutex_lock(&keys_lock);
  pthread_mutex_lock(&runtime_exit_lock);
  runtime_stdio_fork_lock();
  allocator_lock_acquire();
  int child = api_fork();
  allocator_lock_release();
  runtime_stdio_fork_unlock(child == 0);
  pthread_mutex_unlock(&runtime_exit_lock);
  pthread_mutex_unlock(&keys_lock);
  pthread_mutex_unlock(&runtime_environment_lock);
  if (child == 0)
    runtime_thread_after_fork();
  return child;
}
