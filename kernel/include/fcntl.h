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
#define O_DIRECTORY 0x1000
#define O_NONBLOCK 0x2000
#define O_NDELAY O_NONBLOCK
#define O_NOFOLLOW 0x4000
#define O_SYNC 0x8000
#define O_CLOEXEC 0x10000
#define O_FSYNC O_SYNC
#define O_DSYNC O_SYNC

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define F_DUPFD_CLOEXEC 17
#define FD_CLOEXEC 1

#define F_GETLK64 F_GETLK
#define F_SETLK64 F_SETLK
#define F_SETLKW64 F_SETLKW

#endif
#endif
