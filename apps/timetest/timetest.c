#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      logkf("TIMETEST FAIL line=%d\n", __LINE__);                              \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(void) {
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
  sleep(1100);
  time_t last = time(NULL);
  CHECK(last > first && last - first < 10);
  char output[64];
  localtime_r(&last, &calendar);
  strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S", &calendar);
  logkf("TIMETEST PASS local=%s UTC+08:00\n", output);
  return 0;
}
