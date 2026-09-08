#ifndef PLANT_TIME_H
#define PLANT_TIME_H
#include <ctypes.h>
#include <locale.h>
typedef int64_t time_t;
typedef long clock_t;
typedef int clockid_t;
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define TIMER_ABSTIME 1
#define TIME_UTC 1
#ifdef __cplusplus
extern "C" {
#endif
time_t time(time_t *timer);
clock_t clock(void);
uint64_t monotonic_ns(void);
int clock_gettime(clockid_t clock, struct timespec *value);
int clock_getres(clockid_t clock, struct timespec *value);
int timespec_get(struct timespec *value, int base);
int nanosleep(const struct timespec *duration, struct timespec *remaining);
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *duration,
                    struct timespec *remaining);
time_t mktime(struct tm *tm);
time_t timegm(struct tm *tm);
double difftime(time_t t1, time_t t0);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *t);
size_t strftime_l(char *s, size_t max, const char *fmt, const struct tm *t,
                  locale_t locale);
struct tm *localtime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
struct tm *localtime_r(const time_t *timer, struct tm *result);
struct tm *gmtime_r(const time_t *timer, struct tm *result);
char *asctime(const struct tm *time);
char *asctime_r(const struct tm *time, char *buffer);
char *ctime(const time_t *time);
char *ctime_r(const time_t *time, char *buffer);
#ifdef __cplusplus
}
#endif
#define CLOCKS_PER_SEC 1000
#endif
