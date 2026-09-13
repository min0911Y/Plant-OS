#ifndef PLANT_VFS_STAT_H
#define PLANT_VFS_STAT_H
#include <ctypes.h>

enum { VFS_STAT_PIPE = 0x100 };

typedef struct {
  uint32_t type, attributes, size, modified_time;
  uint64_t device, inode;
} vfs_file_stat_t;
#ifdef __cplusplus
static_assert(sizeof(vfs_file_stat_t) == 32, "VFS stat ABI");
#else
_Static_assert(sizeof(vfs_file_stat_t) == 32, "VFS stat ABI");
#endif
#endif
