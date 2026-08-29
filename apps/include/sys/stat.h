#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <fcntl.h>
#include <ctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)

struct stat {
  mode_t st_mode;
  uint32_t st_size;
  uint32_t st_mtime;
};

int stat(const char *path, struct stat *status);
int fstat(int descriptor, struct stat *status);
int mkdir(const char *path, ...);

#ifdef __cplusplus
}
#endif

#endif
