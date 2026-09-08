#ifndef PLANT_UNISTD_H
#define PLANT_UNISTD_H

#include <fcntl.h>
#include <sys/types.h>

enum {
  _SC_PAGESIZE,
  _SC_NPROCESSORS_ONLN,
  _SC_NPROCESSORS_CONF,
  _SC_PHYS_PAGES,
  _SC_AVPHYS_PAGES,
  _SC_OPEN_MAX,
  _SC_GETPW_R_SIZE_MAX,
};
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#ifdef __cplusplus
extern "C" {
#endif
long sysconf(int name);
int getpagesize(void);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int gethostname(char *buffer, size_t size);
const char *getexecname(void);
int isatty(int descriptor);
int access(const char *path, int mode);
int ftruncate(int descriptor, off_t length);
int usleep(useconds_t microseconds);
int chdir(const char *path);
char *getcwd(char *buffer, size_t capacity);
int unlink(const char *path);
int rmdir(const char *path);
int fork(void);
void _exit(int status) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
