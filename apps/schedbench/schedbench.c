#include <errno.h>
#include <futex.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <task.h>

/* All workers remain parked until the owner finishes timing. Thread teardown,
 * allocation and serial output must not contaminate the measured interval. */
typedef struct {
  pthread_t thread;
  uint32_t tid, gate, done, release;
  unsigned iterations, result;
} worker_t;

static void require(int condition, const char *message) {
  if (!condition) {
    logkf("SCHEDBENCH FAIL: %s\n", message);
    exit(1);
  }
}

static void await(uint32_t *word, uint32_t expected) {
  uint64_t deadline = monotonic_ns() + 60000000000ull;
  for (;;) {
    uint32_t value = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    if (value == expected)
      return;
    int result = os_futex_wait(word, value, deadline);
    require(result == FUTEX_OK || result == FUTEX_CHANGED ||
                result == FUTEX_INTERRUPTED,
            "wait timeout or failure");
  }
}

static void publish(uint32_t *word, uint32_t value) {
  __atomic_store_n(word, value, __ATOMIC_RELEASE);
  require(os_futex_wake(word, INT_MAX) >= 0, "wake");
}

static unsigned calculate(unsigned rounds) {
  unsigned value = 1;
  for (unsigned i = 0; i < rounds; i++) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
  }
  return value;
}

static void *compute(void *argument) {
  worker_t *worker = argument;
  __atomic_store_n(&worker->tid, NowTaskID(), __ATOMIC_RELEASE);
  await(&worker->gate, 1);
  worker->result = calculate(worker->iterations);
  publish(&worker->done, 1);
  await(&worker->release, 1);
  return NULL;
}

static void *exchange(void *argument) {
  worker_t *worker = argument;
  __atomic_store_n(&worker->tid, NowTaskID(), __ATOMIC_RELEASE);
  for (unsigned i = 0; i < worker->iterations; i++) {
    await(&worker->gate, 1);
    worker->result++;
    publish(&worker->gate, 0);
  }
  publish(&worker->done, 1);
  await(&worker->release, 1);
  return NULL;
}

static void *sleeper(void *argument) {
  worker_t *worker = argument;
  __atomic_store_n(&worker->tid, NowTaskID(), __ATOMIC_RELEASE);
  while (!__atomic_load_n(&worker->release, __ATOMIC_ACQUIRE)) {
    int result = os_futex_wait(&worker->release, 0, UINT64_MAX);
    require(result == FUTEX_OK || result == FUTEX_CHANGED ||
                result == FUTEX_INTERRUPTED, "park");
  }
  return NULL;
}

static worker_t *start(unsigned count, void *(*entry)(void *), unsigned rounds) {
  worker_t *workers = calloc(count, sizeof(*workers));
  require(workers != NULL, "worker allocation");
  pthread_attr_t attr;
  require(!pthread_attr_init(&attr) &&
              !pthread_attr_setstacksize(&attr, 64 * 1024), "stack attributes");
  for (unsigned i = 0; i < count; i++) {
    workers[i].iterations = rounds;
    require(!pthread_create(&workers[i].thread, &attr, entry, &workers[i]),
            "thread creation");
  }
  pthread_attr_destroy(&attr);
  /* A readiness counter alone does not prove a thread has actually blocked. */
  uint64_t deadline = monotonic_ns() + 60000000000ull;
  for (;;) {
    task_info_t *tasks = NULL;
    size_t task_count = 0;
    require(!task_list(&tasks, &task_count), "task snapshot");
    unsigned waiting = 0;
    for (unsigned i = 0; i < count; i++) {
      unsigned tid = __atomic_load_n(&workers[i].tid, __ATOMIC_ACQUIRE);
      const task_info_t *task = task_snapshot_find(tasks, task_count, tid);
      waiting += tid && task && task->state == TASK_INFO_WAITING;
    }
    free(tasks);
    if (waiting == count)
      break;
    require(monotonic_ns() < deadline, "workers did not park");
    sleep(10);
  }
  return workers;
}

static void stop(worker_t *workers, unsigned count) {
  for (unsigned i = 0; i < count; i++)
    publish(&workers[i].release, 1);
  for (unsigned i = 0; i < count; i++)
    require(!pthread_join(workers[i].thread, NULL), "thread join");
  free(workers);
}

static int compare(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

static void measure(const char *phase, unsigned sleepers, unsigned repeats) {
  enum { ROUNDS = 1000, COMPUTE_ROUNDS = 2000000 };
  uint64_t samples[ROUNDS];
  unsigned cpus = cpu_count();
  unsigned expected = calculate(COMPUTE_ROUNDS);
  for (unsigned repeat = 0; repeat <= repeats; repeat++) {
    worker_t *peer = start(1, exchange, ROUNDS);
    uint64_t begin = monotonic_ns();
    for (unsigned i = 0; i < ROUNDS; i++) {
      uint64_t sent = monotonic_ns();
      publish(&peer->gate, 1);
      await(&peer->gate, 0);
      samples[i] = monotonic_ns() - sent;
      require(peer->result == i + 1, "handoff payload");
    }
    uint64_t handoff = monotonic_ns() - begin;
    await(&peer->done, 1);
    stop(peer, 1);
    qsort(samples, ROUNDS, sizeof(samples[0]), compare);
    begin = monotonic_ns();
    for (unsigned i = 0; i < ROUNDS; i++)
      api_yield();
    uint64_t yielding = monotonic_ns() - begin;
    worker_t *workers = start(cpus, compute, COMPUTE_ROUNDS);
    begin = monotonic_ns();
    for (unsigned i = 0; i < cpus; i++)
      publish(&workers[i].gate, 1);
    for (unsigned i = 0; i < cpus; i++)
      await(&workers[i].done, 1);
    uint64_t computation = monotonic_ns() - begin;
    for (unsigned i = 0; i < cpus; i++)
      require(workers[i].result == expected, "compute checksum");
    stop(workers, cpus);
    if (repeat)
      logkf("SCHEDBENCH phase=%s sleepers=%u repeat=%u cpus=%u rounds=%u "
            "handoff_ns=%llu p50_ns=%llu p95_ns=%llu p99_ns=%llu "
            "yield_ns=%llu compute_ns=%llu work=%u\n",
            phase, sleepers, repeat, cpus, ROUNDS,
            (unsigned long long)handoff, (unsigned long long)samples[ROUNDS / 2],
            (unsigned long long)samples[ROUNDS * 95 / 100],
            (unsigned long long)samples[ROUNDS * 99 / 100],
            (unsigned long long)yielding, (unsigned long long)computation,
            COMPUTE_ROUNDS);
  }
}

int main(int argc, char **argv) {
  unsigned sleepers = 256, repeats = 7;
  require(argc <= 3, "usage: schbench.bin [sleepers] [repeats]");
  for (int i = 1; i < argc; i++) {
    char *end;
    errno = 0;
    unsigned long value = strtoul(argv[i], &end, 10);
    require(argv[i][0] >= '0' && argv[i][0] <= '9' && !*end && !errno &&
                value && value <= UINT_MAX && (i != 2 || value < UINT_MAX),
            "positive integer argument required");
    if (i == 1)
      sleepers = value;
    else
      repeats = value;
  }
  measure("before", sleepers, repeats);
  worker_t *parked = start(sleepers, sleeper, 0);
  measure("parked", sleepers, repeats);
  stop(parked, sleepers);
  measure("after", sleepers, repeats);
  logk("SCHEDBENCH PASS\n");
  return 0;
}
