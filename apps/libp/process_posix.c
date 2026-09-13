#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

char **environ;

int getgroups(int count, gid_t groups[]) {
  if (count < 0 || (count != 0 && groups == NULL)) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

static int unsupported(void) {
  errno = ENOTSUP;
  return -1;
}

int pipe(int descriptors[2]) {
  (void)descriptors;
  return unsupported();
}

int pipe2(int descriptors[2], int flags) {
  (void)descriptors;
  (void)flags;
  return unsupported();
}

int dup(int descriptor) {
  (void)descriptor;
  return unsupported();
}

int dup2(int source, int destination) {
  if (source == destination && source >= 0)
    return destination;
  (void)source;
  (void)destination;
  return unsupported();
}

int execve(const char *path, char *const argv[], char *const envp[]) {
  (void)path;
  (void)argv;
  (void)envp;
  return unsupported();
}

int execv(const char *path, char *const argv[]) {
  return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
  return execve(file, argv, environ);
}

int vfork(void) { return fork(); }

ssize_t readlink(const char *path, char *buffer, size_t capacity) {
  (void)path;
  (void)buffer;
  (void)capacity;
  return unsupported();
}

long pathconf(const char *path, int name) {
  (void)path;
  if (name == _PC_NAME_MAX)
    return NAME_MAX;
  errno = EINVAL;
  return -1;
}

int chmod(const char *path, mode_t mode) {
  (void)path;
  (void)mode;
  return unsupported();
}

int fchmod(int descriptor, mode_t mode) {
  (void)descriptor;
  (void)mode;
  return unsupported();
}

int chown(const char *path, uid_t owner, gid_t group) {
  (void)path;
  (void)owner;
  (void)group;
  return unsupported();
}

int lchown(const char *path, uid_t owner, gid_t group) {
  return chown(path, owner, group);
}

int fchown(int descriptor, uid_t owner, gid_t group) {
  (void)descriptor;
  (void)owner;
  (void)group;
  return unsupported();
}

int futimes(int descriptor, const struct timeval times[2]) {
  (void)descriptor;
  (void)times;
  return unsupported();
}

int lutimes(const char *path, const struct timeval times[2]) {
  (void)path;
  (void)times;
  return unsupported();
}

int link(const char *existing, const char *newpath) {
  (void)existing;
  (void)newpath;
  return unsupported();
}

int symlink(const char *target, const char *linkpath) {
  (void)target;
  (void)linkpath;
  return unsupported();
}

int mknod(const char *path, mode_t mode, dev_t device) {
  (void)path;
  (void)mode;
  (void)device;
  return unsupported();
}

int utimes(const char *path, const struct timeval times[2]) {
  (void)path;
  (void)times;
  return unsupported();
}

mode_t umask(mode_t mode) {
  (void)mode;
  return 0;
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attributes, char *const argv[],
                char *const envp[]) {
  (void)pid;
  (void)path;
  (void)file_actions;
  (void)attributes;
  (void)argv;
  (void)envp;
  return ENOTSUP;
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attributes, char *const argv[],
                 char *const envp[]) {
  (void)pid;
  (void)file;
  (void)file_actions;
  (void)attributes;
  (void)argv;
  (void)envp;
  return ENOTSUP;
}

int statvfs(const char *path, struct statvfs *status) {
  (void)path;
  (void)status;
  return unsupported();
}

int fstatvfs(int descriptor, struct statvfs *status) {
  (void)descriptor;
  (void)status;
  return unsupported();
}
