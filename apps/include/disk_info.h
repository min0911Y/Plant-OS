#ifndef PLANT_DISK_INFO_H
#define PLANT_DISK_INFO_H
#include <ctypes.h>

enum { DISK_INFO_BLOCK = 1, DISK_INFO_OPTICAL = 2, DISK_INFO_ERROR_SIZE = 192 };
typedef struct {
  uint64_t capacity_bytes; /* Zero means unknown or not applicable. */
  uint32_t disk, type, writable,
      mount_drive; /* Drive letters; zero if unmounted. */
  char name[64];
  char filesystem[32];
  char error[DISK_INFO_ERROR_SIZE]; /* First I/O failure; empty while healthy. */
} disk_info_t;
#ifdef __cplusplus
static_assert(sizeof(disk_info_t) == 312, "disk info ABI");
#else
_Static_assert(sizeof(disk_info_t) == 312, "disk info ABI");
#endif
#endif
