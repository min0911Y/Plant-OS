#ifndef __Fcntl
#define __Fcntl
int open(const char *pathname, int flags, u32 mode);
int close(int fd);
u32 read(int fd, void *buf, u32 count);
u32 write(int fd, const void *buf, u32 nbyte);
#define O_RDONLY 0
#define O_WRONLY 0
#define O_CREAT  0
#define O_TRUNC  0
#define O_APPEND 0
#endif