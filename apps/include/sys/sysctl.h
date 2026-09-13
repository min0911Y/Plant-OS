#ifndef PLANT_SYS_SYSCTL_H
#define PLANT_SYS_SYSCTL_H

#include <ctypes.h>

#define CTL_KERN 1
#define CTL_HW 6

#define KERN_OSTYPE 1
#define KERN_OSRELEASE 2
#define KERN_BOOTTIME 21

#define HW_MACHINE 1
#define HW_MODEL 2
#define HW_NCPU 3
#define HW_PHYSMEM 5
#define HW_MEMSIZE HW_PHYSMEM
#define HW_CPU_FREQ 15

typedef struct xsw_usage {
  uint64_t xsu_total;
  uint64_t xsu_avail;
  uint64_t xsu_used;
  uint32_t xsu_pagesize;
  int xsu_encrypted;
} xsw_usage;

#ifdef __cplusplus
extern "C" {
#endif

int sysctl(const int *name, unsigned name_length, void *old_value,
           size_t *old_length, const void *new_value, size_t new_length);
int sysctlbyname(const char *name, void *old_value, size_t *old_length,
                 const void *new_value, size_t new_length);

#ifdef __cplusplus
}
#endif

#endif
