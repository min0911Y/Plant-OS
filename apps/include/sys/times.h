#ifndef PLANT_SYS_TIMES_H
#define PLANT_SYS_TIMES_H

#include <time.h>

struct tms {
  clock_t tms_utime;
  clock_t tms_stime;
  clock_t tms_cutime;
  clock_t tms_cstime;
};

#ifdef __cplusplus
extern "C" {
#endif
clock_t times(struct tms *times);
#ifdef __cplusplus
}
#endif

#endif
