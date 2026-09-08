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
#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval *time, void *timezone);
#ifdef __cplusplus
}
#endif
#endif
