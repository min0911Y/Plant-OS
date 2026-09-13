#ifndef PLANT_SYS_TIME_H
#define PLANT_SYS_TIME_H
#include <sys/types.h>

struct timeval {
  time_t tv_sec;
  suseconds_t tv_usec;
};
struct timezone {
  int tz_minuteswest, tz_dsttime;
};
struct itimerval {
  struct timeval it_interval;
  struct timeval it_value;
};
#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2
#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval *time, void *timezone);
int setitimer(int which, const struct itimerval *value,
              struct itimerval *old_value);
int getitimer(int which, struct itimerval *value);
#ifdef __cplusplus
}
#endif
#endif
