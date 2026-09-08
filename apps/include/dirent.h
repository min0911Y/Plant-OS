#ifndef PLANT_DIRENT_H
#define PLANT_DIRENT_H
#include <sys/types.h>

typedef struct __directory DIR;
enum {
  DT_UNKNOWN = 0,
  DT_FIFO = 1,
  DT_CHR = 2,
  DT_DIR = 4,
  DT_BLK = 6,
  DT_REG = 8,
  DT_LNK = 10,
  DT_SOCK = 12
};
#define _DIRENT_HAVE_D_TYPE 1
struct dirent {
  ino_t d_ino;
  off_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};
#ifdef __cplusplus
extern "C" {
#endif
DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
void rewinddir(DIR *directory);
int dirfd(DIR *directory);
#ifdef __cplusplus
}
#endif
#endif
