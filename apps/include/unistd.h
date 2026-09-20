#ifndef PLANT_UNISTD_H
#define PLANT_UNISTD_H

#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

enum {
  _SC_PAGESIZE,
  _SC_NPROCESSORS_ONLN,
  _SC_NPROCESSORS_CONF,
  _SC_PHYS_PAGES,
  _SC_AVPHYS_PAGES,
  _SC_OPEN_MAX,
  _SC_GETPW_R_SIZE_MAX,
  _SC_GETGR_R_SIZE_MAX,
  _SC_IOV_MAX,
};
enum { _PC_NAME_MAX };
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
extern char **environ;
long sysconf(int name);
int getpagesize(void);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int getpeereid(int descriptor, uid_t *euid, gid_t *egid);
int gethostname(char *buffer, size_t size);
int getgroups(int count, gid_t groups[]);
const char *getexecname(void);
int isatty(int descriptor);
int access(const char *path, int mode);
int ftruncate(int descriptor, off_t length);
ssize_t pread(int descriptor, void *buffer, size_t count, off_t offset);
ssize_t pwrite(int descriptor, const void *buffer, size_t count, off_t offset);
ssize_t readlink(const char *path, char *buffer, size_t capacity);
long pathconf(const char *path, int name);
int usleep(useconds_t microseconds);
int chdir(const char *path);
int fchdir(int descriptor);
char *getcwd(char *buffer, size_t capacity);
int unlink(const char *path);
int rmdir(const char *path);
int fork(void);
int vfork(void);
int pipe(int descriptors[2]);
int pipe2(int descriptors[2], int flags);
int dup(int descriptor);
int dup2(int source, int destination);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int chmod(const char *path, mode_t mode);
int fchmod(int descriptor, mode_t mode);
int chown(const char *path, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int fchown(int descriptor, uid_t owner, gid_t group);
int futimes(int descriptor, const struct timeval times[2]);
int lutimes(const char *path, const struct timeval times[2]);
int link(const char *existing, const char *newpath);
int symlink(const char *target, const char *linkpath);
int mknod(const char *path, mode_t mode, dev_t device);
int utimes(const char *path, const struct timeval times[2]);
mode_t umask(mode_t mode);
void _exit(int status) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
