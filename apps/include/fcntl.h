#ifndef __FCNTL__
#define __FCNTL__
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_EXCL      0x0800
#define O_DIRECTORY 0x1000
int open(const char *pathname, int flags, ...);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int close(int fd);
int fsync(int fd);
#ifdef __cplusplus
}
#endif
#endif
