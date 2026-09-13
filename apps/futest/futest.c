#include <errno.h>
#include <futex.h>
#include <ipc.h>
#include <limits.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>
#include <time.h>
#include <vm.h>

enum { STACK_SIZE = 64 * 1024, WORKERS = 4, ROUNDS = 1000 };
static unsigned parent;
static int failures;

typedef struct {
  void *stack;
  int tid;
} test_thread_t;

typedef struct {
  uint32_t *word;
  uint64_t deadline;
} wait_request_t;

typedef struct {
  uint32_t turn;
  unsigned payload;
  uint64_t deadline;
} exchange_t;

int futex_syscall(unsigned operation, const futex_request_t *request,
                  size_t size);

static void check(int condition, const char *name) {
  if (!condition) {
    failures++;
    logkf("FUTEXTEST FAIL: %s\n", name);
  }
}

static int start_thread(test_thread_t *thread, void (*entry)(uintptr_t),
                        uintptr_t argument) {
  thread->stack = vm_map(NULL, STACK_SIZE);
  thread->tid = thread->stack
                    ? AddThread("futex-test", (uintptr_t)entry,
                                (uintptr_t)thread->stack + STACK_SIZE, argument)
                    : -1;
  check(thread->tid > 0, "create thread");
  return thread->tid > 0;
}

static void stop_thread(test_thread_t *thread) {
  if (thread->tid > 0)
    SubThread(thread->tid);
  if (thread->stack)
    check(vm_unmap(thread->stack, STACK_SIZE) == 0, "release thread stack");
}

static int await_waiting(int tid) {
  uint64_t deadline = monotonic_ns() + 5000000000ull;
  while (monotonic_ns() < deadline) {
    task_info_t *tasks = NULL;
    size_t count = 0;
    if (task_list(&tasks, &count) < 0)
      break;
    const task_info_t *task = task_snapshot_find(tasks, count, tid);
    int waiting = task && task->state == TASK_INFO_WAITING;
    free(tasks);
    if (waiting)
      return 1;
    sleep(1);
  }
  check(0, "thread entered blocking wait");
  return 0;
}

static _Noreturn void finish_worker(int result) {
  ipc_send_to(parent, 0, 0, &result, sizeof(result), 5000);
  /* Retain the TID until the owner stops this thread and releases its stack. */
  uint32_t parked = 0;
  for (;;)
    os_futex_wait(&parked, 0, UINT64_MAX);
}

static void wait_worker(uintptr_t argument) {
  const wait_request_t *request = (const wait_request_t *)argument;
  int result;
  do {
    result = os_futex_wait(request->word, 0, request->deadline);
  } while (result == FUTEX_INTERRUPTED);
  finish_worker(result);
}

static int receive_result(unsigned tid, int expected) {
  ipc_msg_t message;
  int result = FUTEX_INVALID;
  int received = ipc_recv_from(tid, &result, sizeof(result), &message, 20000);
  if (received != sizeof(result) || result != expected)
    logkf("FUTEXTEST result tid=%u received=%d value=%d expected=%d\n", tid,
          received, result, expected);
  return received == sizeof(result) && result == expected;
}

static void validate_arguments(void) {
  uint32_t word = 0;
  check(os_futex_wait(&word, 1, 0) == FUTEX_CHANGED,
        "value change takes precedence over deadline");
  check(os_futex_wait(&word, 0, 0) == FUTEX_TIMED_OUT, "expired deadline");
  check(os_futex_wait(NULL, 0, 0) == FUTEX_FAULT, "null address");
  check(os_futex_wait((uint32_t *)((char *)&word + 1), 0, 0) == FUTEX_INVALID,
        "unaligned address");
  check(os_futex_wait((uint32_t *)(UINTPTR_MAX - 3), 0, 0) == FUTEX_FAULT,
        "address overflow");
  check(os_futex_wake(&word, -1) == FUTEX_INVALID, "negative wake count");
  futex_request_t request = {0, (uintptr_t)&word, UINT32_MAX};
  check(futex_syscall(FUTEX_WAKE, &request, sizeof(request)) == FUTEX_INVALID,
        "invalid raw wake count");
  check(futex_syscall(FUTEX_OPERATION_COUNT, &request, sizeof(request)) ==
            FUTEX_INVALID,
        "invalid operation");
  check(futex_syscall(FUTEX_WAIT, &request, sizeof(request) - 1) ==
            FUTEX_INVALID,
        "invalid request size");
  check(futex_syscall(FUTEX_WAIT, NULL, sizeof(request)) == FUTEX_FAULT,
        "unreadable request");

  char *pages = vm_map(NULL, 2 * VM_PAGE_SIZE);
  check(pages != NULL, "validation mapping");
  if (!pages)
    return;
  check(vm_unmap(pages + VM_PAGE_SIZE, VM_PAGE_SIZE) == 0, "validation hole");
  check(futex_syscall(FUTEX_WAIT, (futex_request_t *)(pages + VM_PAGE_SIZE - 4),
                      sizeof(request)) == FUTEX_FAULT,
        "request spans unmapped page");
  check(os_futex_wait((uint32_t *)(pages + VM_PAGE_SIZE), 0, 0) == FUTEX_FAULT,
        "unmapped word");
  check(vm_protect(pages, VM_PAGE_SIZE, VM_READ) == 0, "read-only word");
  check(os_futex_wait((uint32_t *)pages, 1, 0) == FUTEX_CHANGED,
        "wait does not require write access");
  check(vm_unmap(pages, VM_PAGE_SIZE) == 0, "release validation mapping");
}

static void timeout_and_messages(void) {
  uint32_t word = 0;
  int token = 42;
  check(ipc_send_to(parent, 0, 0, &token, sizeof(token), 5000) == IPC_OK,
        "queue application message");
  uint64_t deadline = monotonic_ns() + 30000000ull;
  int result;
  do {
    result = os_futex_wait(&word, 0, deadline);
  } while (result == FUTEX_INTERRUPTED);
  check(result == FUTEX_TIMED_OUT && monotonic_ns() >= deadline,
        "blocking monotonic timeout");
  check(receive_result(parent, token), "wait preserves application messages");
  check(os_futex_wake(&word, INT_MAX) == 0, "timeout removed waiter");
}

static void wake_and_isolation(void) {
  uint32_t words[2] = {0};
  wait_request_t requests[WORKERS];
  test_thread_t threads[WORKERS] = {0};
  unsigned started = 0;
  for (; started < WORKERS; started++) {
    requests[started] = (wait_request_t){&words[started == WORKERS - 1],
                                         monotonic_ns() + 10000000000ull};
    if (!start_thread(&threads[started], wait_worker,
                      (uintptr_t)&requests[started]) ||
        !await_waiting(threads[started].tid))
      break;
  }
  if (started == WORKERS) {
    int child = fork();
    if (child == 0)
      _exit(os_futex_wake(&words[0], INT_MAX) != 0);
    check(child > 0 && waittid(child) == 0, "fork has independent futex keys");
    check(os_futex_wake(&words[0], 0) == 0, "zero wake count");
    check(os_futex_wake(&words[0], 1) == 1, "wake exactly one");
    check(receive_result(IPC_ANY_TID, FUTEX_OK), "first wake result");
    check(ipc_pending() == 0, "other waiters remain blocked");
    check(os_futex_wake(&words[0], INT_MAX) == WORKERS - 2, "wake remaining");
    for (unsigned i = 0; i < WORKERS - 2; i++)
      check(receive_result(IPC_ANY_TID, FUTEX_OK), "remaining wake result");
    check(os_futex_wake(&words[1], INT_MAX) == 1, "independent address");
    check(receive_result(threads[WORKERS - 1].tid, FUTEX_OK),
          "independent address result");
    check(os_futex_wake(&words[0], INT_MAX) == 0, "wake removed waiters");
  }
  for (unsigned i = 0; i < WORKERS; i++)
    stop_thread(&threads[i]);
}

static void semaphore_worker(uintptr_t argument) {
  sem_t *semaphore = (sem_t *)argument;
  int result;
  do {
    result = sem_wait(semaphore);
  } while (result && errno == EINTR);
  finish_worker(result);
}

static void semaphore_burst(void) {
  for (unsigned round = 0; round < 8; round++) {
    sem_t semaphore;
    check(sem_init(&semaphore, 0, 0) == 0, "initialize semaphore");
    test_thread_t threads[WORKERS] = {0};
    unsigned started = 0;
    for (; started < WORKERS; started++) {
      if (!start_thread(&threads[started], semaphore_worker,
                        (uintptr_t)&semaphore) ||
          !await_waiting(threads[started].tid))
        break;
    }
    if (started == WORKERS) {
      for (unsigned i = 0; i < WORKERS; i++)
        check(sem_post(&semaphore) == 0, "post semaphore burst");
      for (unsigned i = 0; i < WORKERS; i++)
        check(receive_result(threads[i].tid, 0), "every semaphore waiter wakes");
      int remaining = -1;
      check(sem_getvalue(&semaphore, &remaining) == 0 && remaining == 0 &&
                sem_trywait(&semaphore) == -1 && errno == EAGAIN,
            "semaphore consumes each posted token once");
    }
    for (unsigned i = 0; i < WORKERS; i++)
      stop_thread(&threads[i]);
    check(sem_destroy(&semaphore) == 0, "destroy semaphore");
  }
}

static int exchange_wait(exchange_t *exchange, uint32_t turn) {
  for (;;) {
    uint32_t value = __atomic_load_n(&exchange->turn, __ATOMIC_ACQUIRE);
    if (value == turn)
      return 1;
    int result = os_futex_wait(&exchange->turn, value, exchange->deadline);
    if (result != FUTEX_OK && result != FUTEX_CHANGED &&
        result != FUTEX_INTERRUPTED)
      return 0;
  }
}

static void exchange_worker(uintptr_t argument) {
  exchange_t *exchange = (exchange_t *)argument;
  int result = FUTEX_OK;
  for (unsigned i = 0; i < ROUNDS; i++) {
    if (!exchange_wait(exchange, 1) || exchange->payload != 2 * i + 1) {
      result = FUTEX_INVALID;
      break;
    }
    exchange->payload++;
    __atomic_store_n(&exchange->turn, 0, __ATOMIC_RELEASE);
    os_futex_wake(&exchange->turn, 1);
  }
  finish_worker(result);
}

static void exchange_race(void) {
  exchange_t exchange = {.deadline = monotonic_ns() + 20000000000ull};
  test_thread_t thread = {0};
  if (start_thread(&thread, exchange_worker, (uintptr_t)&exchange)) {
    unsigned i = 0;
    for (; i < ROUNDS; i++) {
      if (!exchange_wait(&exchange, 0) || exchange.payload != 2 * i)
        break;
      exchange.payload++;
      __atomic_store_n(&exchange.turn, 1, __ATOMIC_RELEASE);
      os_futex_wake(&exchange.turn, 1);
    }
    check(i == ROUNDS && receive_result(thread.tid, FUTEX_OK) &&
              exchange.payload == 2 * ROUNDS,
          "repeated publish/wait/wake race and memory visibility");
  }
  stop_thread(&thread);
}

static void cancel_waiters(void) {
  uint32_t word = 0;
  for (unsigned i = 0; i < 8; i++) {
    wait_request_t request = {&word, i & 1 ? UINT64_MAX
                                           : monotonic_ns() + 500000000ull};
    test_thread_t thread = {0};
    if (start_thread(&thread, wait_worker, (uintptr_t)&request))
      await_waiting(thread.tid);
    stop_thread(&thread);
    check(os_futex_wake(&word, INT_MAX) == 0, "killed waiter removed");
  }
  sleep(550);
  check(ipc_pending() == 0, "cancelled deadlines do not resume freed stacks");
}

static void allocate_worker(uintptr_t argument) {
  uint32_t *start = (uint32_t *)argument;
  while (!__atomic_load_n(start, __ATOMIC_ACQUIRE))
    os_futex_wait(start, 0, UINT64_MAX);
  unsigned seed = NowTaskID();
  int result = FUTEX_OK;
  for (unsigned i = 0; i < 200; i++) {
    size_t size = (i * 97 + seed) % 8192 + 1;
    unsigned char pattern = (unsigned char)(i + seed);
    unsigned char *data = malloc(size);
    if (!data) {
      result = FUTEX_INVALID;
      break;
    }
    memset(data, pattern, size);
    sleep(1);
    for (size_t j = 0; j < size; j++) {
      if (data[j] != pattern)
        result = FUTEX_INVALID;
    }
    free(data);
    if (result != FUTEX_OK)
      break;
  }
  finish_worker(result);
}

static void concurrent_allocations(void) {
  uint32_t start = 0;
  test_thread_t threads[WORKERS] = {0};
  for (unsigned i = 0; i < WORKERS; i++) {
    if (start_thread(&threads[i], allocate_worker, (uintptr_t)&start))
      await_waiting(threads[i].tid);
  }
  __atomic_store_n(&start, 1, __ATOMIC_RELEASE);
  os_futex_wake(&start, INT_MAX);
  for (unsigned i = 0; i < WORKERS; i++) {
    if (threads[i].tid > 0)
      check(receive_result(threads[i].tid, FUTEX_OK), "concurrent allocator");
    stop_thread(&threads[i]);
  }
}

int main(void) {
  parent = NowTaskID();
  validate_arguments();
  timeout_and_messages();
  wake_and_isolation();
  semaphore_burst();
  exchange_race();
  cancel_waiters();
  concurrent_allocations();
  if (!failures) {
    puts("FUTEXTEST PASS");
    logk("FUTEXTEST PASS\n");
  }
  return failures != 0;
}
