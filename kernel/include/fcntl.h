#ifndef __Fcntl
#define __Fcntl
#ifndef _KERNEL_FCNTL_H
#define _KERNEL_FCNTL_H

int open(const char *pathname, int flags, unsigned int mode);
int close(int fd);
unsigned int read (int fd, void *buf, unsigned int count);
unsigned int write(int fd, const void *buf, unsigned int nbyte);
int lseek(int fd, int offset, int whence);
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_EXCL 0x0800

#endif
#endif
