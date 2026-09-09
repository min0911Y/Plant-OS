#include <errno.h>
#include <futex.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>
#include <time.h>

int64_t __divmoddi4(int64_t numerator, int64_t denominator, int64_t *remainder);

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      logkf("TIMETEST FAIL line=%d\n", __LINE__);                              \
      return 1;                                                                \
    }                                                                          \
  } while (0)

struct clock_test {
  pthread_mutex_t lock;
  uint64_t last;
  uint32_t start;
  unsigned cpus;
  bool *seen;
};

static void *clock_reader(void *argument) {
  struct clock_test *test = argument;
  while (!__atomic_load_n(&test->start, __ATOMIC_ACQUIRE))
    os_futex_wait(&test->start, 0, UINT64_MAX);
  bool valid = true;
  for (unsigned i = 0; i < 4096; i++) {
    pthread_mutex_lock(&test->lock);
    uint64_t now = monotonic_ns();
    valid &= now >= test->last;
    test->last = now;
    if (!(i % 64)) {
      unsigned cpu = cpu_current();
      if (cpu < test->cpus)
        test->seen[cpu] = true;
      else
        valid = false;
    }
    pthread_mutex_unlock(&test->lock);
    if (!(i % 64))
      sched_yield();
  }
  return (void *)(uintptr_t)valid;
}

static int monotonic_clock_test(void) {
  struct clock_test test = {.lock = PTHREAD_MUTEX_INITIALIZER,
                            .cpus = cpu_count()};
  test.seen = calloc(test.cpus, sizeof(*test.seen));
  CHECK(test.seen);
  pthread_t readers[4];
  unsigned created = 0;
  while (created < 4 &&
         pthread_create(&readers[created], NULL, clock_reader, &test) == 0)
    created++;
  __atomic_store_n(&test.start, 1, __ATOMIC_RELEASE);
  os_futex_wake(&test.start, INT_MAX);
  bool valid = created == 4;
  for (unsigned i = 0; i < created; i++) {
    void *result = NULL;
    valid &= pthread_join(readers[i], &result) == 0 && result == (void *)1;
  }
  unsigned observed = 0;
  for (unsigned cpu = 0; cpu < test.cpus; cpu++)
    observed += test.seen[cpu];
  free(test.seen);
  CHECK(pthread_mutex_destroy(&test.lock) == 0 && valid && observed);
  uint64_t start = monotonic_ns();
  for (unsigned i = 0; i < 10000; i++)
    monotonic_ns();
  logkf("TIMETEST monotonic cpus=%u read_ns=%llu\n", observed,
        (unsigned long long)((monotonic_ns() - start) / 10000));
  return 0;
}

int main(void) {
  CHECK(monotonic_clock_test() == 0);
  static const struct {
    int64_t num, den, quotient, remainder;
  } divisions[] = {{0, 86400, 0, 0},
                   {-9, 4, -2, -1},
                   {9, -4, -2, 1},
                   {-9, -4, 2, -1},
                   {INT64_MIN, 1, INT64_MIN, 0},
                   {INT64_MAX, 3, INT64_MAX / 3, INT64_MAX % 3}};
  for (size_t i = 0; i < sizeof(divisions) / sizeof(*divisions); i++) {
    volatile int64_t numerator = divisions[i].num;
    volatile int64_t denominator = divisions[i].den;
    int64_t remainder;
    CHECK(__divmoddi4(numerator, denominator, &remainder) ==
          divisions[i].quotient);
    CHECK(remainder == divisions[i].remainder);
    CHECK(numerator / denominator == divisions[i].quotient);
    CHECK(numerator % denominator == divisions[i].remainder);
  }
  static const struct {
    time_t timestamp;
    const char *utc;
    int weekday, yearday;
  } cases[] = {
      {0, "1970-01-01 00:00:00", 4, 0},
      {951782400, "2000-02-29 00:00:00", 2, 59},
      {1709251199, "2024-02-29 23:59:59", 4, 59},
      {1709251200, "2024-03-01 00:00:00", 5, 60},
      {4107542400u, "2100-03-01 00:00:00", 1, 59},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    struct tm calendar;
    char output[64];
    CHECK(gmtime_r(&cases[i].timestamp, &calendar) == &calendar);
    CHECK(strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S", &calendar) ==
          19);
    CHECK(strcmp(output, cases[i].utc) == 0);
    CHECK(calendar.tm_wday == cases[i].weekday &&
          calendar.tm_yday == cases[i].yearday);
    localtime_r(&cases[i].timestamp, &calendar);
    CHECK(mktime(&calendar) == cases[i].timestamp);
    CHECK(strftime(output, 0, "%Y", &calendar) == 0);
    CHECK(strftime(output, 4, "%Y", &calendar) == 0);
  }
  struct tm calendar = {
      .tm_year = 124, .tm_mon = 1, .tm_mday = 30, .tm_hour = 8};
  CHECK(mktime(&calendar) == 1709251200);
  CHECK(calendar.tm_mon == 2 && calendar.tm_mday == 1 &&
        calendar.tm_yday == 60);
  CHECK(difftime(0, 1) == -1.0);
  time_t first = time(NULL);
  for (unsigned i = 0; first == (time_t)-1 && i < 10; i++) {
    sleep(10);
    first = time(NULL);
  }
  CHECK(first != (time_t)-1);
  gmtime_r(&first, &calendar);
  CHECK(calendar.tm_year + 1900 == get_year());
  CHECK(calendar.tm_year >= 125 && calendar.tm_year < 200);
  uint64_t started_ns = monotonic_ns();
  clock_t started_ms = clock();
  sleep(1100);
  uint64_t elapsed_ns = monotonic_ns() - started_ns;
  uint64_t elapsed_ms = (clock_t)(clock() - started_ms);
  /* The interrupt-driven millisecond clock is independent of pvclock's
   * conversion. Check the rate as well as monotonicity across threads. */
  CHECK(elapsed_ms >= 100 &&
        elapsed_ns >= (elapsed_ms - 100) * 1000000ull &&
        elapsed_ns <= (elapsed_ms + 100) * 1000000ull);
  time_t last = time(NULL);
  CHECK(last > first && last - first < 10);
  char output[64];
  localtime_r(&last, &calendar);
  strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S", &calendar);
  logkf("TIMETEST PASS local=%s UTC+08:00\n", output);
  return 0;
}
