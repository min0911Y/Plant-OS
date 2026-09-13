#ifndef PLANT_SYS_RESOURCE_H
#define PLANT_SYS_RESOURCE_H

#include <sys/types.h>

typedef uint64_t rlim_t;

struct rlimit {
  rlim_t rlim_cur;
  rlim_t rlim_max;
};

#define RLIM_INFINITY ((rlim_t)-1)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY

enum {
  RLIMIT_CPU,
  RLIMIT_FSIZE,
  RLIMIT_DATA,
  RLIMIT_STACK,
  RLIMIT_CORE,
  RLIMIT_RSS,
  RLIMIT_NPROC,
  RLIMIT_NOFILE,
  RLIMIT_MEMLOCK,
  RLIMIT_AS,
  RLIMIT_NLIMITS,
};

#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2
#define PRIO_MIN (-20)
#define PRIO_MAX 20

#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int resource, struct rlimit *limits);
int setrlimit(int resource, const struct rlimit *limits);
int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int priority);
#ifdef __cplusplus
}
#endif

#endif
