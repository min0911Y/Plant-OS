#ifndef PLANT_UTIME_H
#define PLANT_UTIME_H

#include <time.h>

struct utimbuf {
  time_t actime;
  time_t modtime;
};

#ifdef __cplusplus
extern "C" {
#endif

int utime(const char *path, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif
