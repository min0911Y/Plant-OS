#ifndef __TIME__H__
#define __TIME__H__
#include <ctypes.h>
typedef long clock_t;
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

typedef struct {
  uint64_t sec;
  uint64_t nsec;
} time_ns_t;

typedef unsigned int time_t;
time_t               time(time_t timer);
void                 gettime_ns(time_ns_t *ptr);
static clock_t       clock() {
  time_ns_t time_ns;
  gettime_ns(&time_ns);
  return time_ns.sec * 1000 + time_ns.nsec / 1000000;
}
void       clock_gettime(int *sec1, int *usec1);
time_t     mktime(struct tm *tm);
size_t     strftime(char *s, size_t max, const char *fmt, const struct tm *t);
struct tm *localtime(time_t *t1);
#define CLOCKS_PER_SEC 1000
#endif