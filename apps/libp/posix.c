#include "runtime_lifecycle.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <task.h>
#include <unistd.h>
#include <vm.h>

extern const runtime_linker_t *runtime_linker;
const char *getexecname(void) {
  return runtime_linker ? runtime_linker->executable_path : NULL;
}

static struct passwd root_user = {"root",     "",  0,         0,
                                  "Plant OS", "/", "/psh.bin"};
struct passwd *getpwuid(uid_t uid) {
  return uid == 0 ? &root_user : NULL;
}
struct passwd *getpwnam(const char *name) {
  return name && !strcmp(name, root_user.pw_name) ? &root_user : NULL;
}
int getpwnam_r(const char *name, struct passwd *entry, char *buffer,
               size_t size, struct passwd **result) {
  *result = NULL;
  struct passwd *user = getpwnam(name);
  if (!user)
    return 0;
  const char *strings[] = {user->pw_name, user->pw_passwd, user->pw_gecos,
                           user->pw_dir, user->pw_shell};
  size_t required = 0;
  for (unsigned i = 0; i < 5; i++)
    required += strlen(strings[i]) + 1;
  if (size < required)
    return ERANGE;
  *entry = *user;
  char **fields[] = {&entry->pw_name, &entry->pw_passwd, &entry->pw_gecos,
                     &entry->pw_dir, &entry->pw_shell};
  for (unsigned i = 0; i < 5; i++) {
    size_t length = strlen(strings[i]) + 1;
    *fields[i] = buffer;
    memcpy(buffer, strings[i], length);
    buffer += length;
  }
  *result = entry;
  return 0;
}

int getpwuid_r(uid_t uid, struct passwd *entry, char *buffer, size_t size,
               struct passwd **result) {
  if (uid) {
    *result = NULL;
    return 0;
  }
  return getpwnam_r("root", entry, buffer, size, result);
}

long sysconf(int name) {
  switch (name) {
  case _SC_PAGESIZE:
    return VM_PAGE_SIZE;
  case _SC_NPROCESSORS_ONLN:
  case _SC_NPROCESSORS_CONF:
    return cpu_count();
  case _SC_PHYS_PAGES:
    return mem_total() / VM_PAGE_SIZE;
  case _SC_AVPHYS_PAGES:
    return mem_total() / VM_PAGE_SIZE - mem_used();
  case _SC_OPEN_MAX:
    return __INT_MAX__;
  case _SC_GETPW_R_SIZE_MAX:
    return 1024;
  default:
    errno = EINVAL;
    return -1;
  }
}
int getpagesize(void) { return VM_PAGE_SIZE; }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }
int gethostname(char *buffer, size_t size) {
  static const char name[] = "plant-os";
  if (!buffer || size < sizeof(name)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(buffer, name, sizeof(name));
  return 0;
}
int isatty(int descriptor) {
  if (descriptor >= 0 && descriptor <= 2)
    return 1;
  struct stat status;
  if (!fstat(descriptor, &status))
    errno = ENOTTY;
  return 0;
}
int usleep(useconds_t microseconds) {
  struct timespec duration = {microseconds / 1000000,
                              (microseconds % 1000000) * 1000};
  return nanosleep(&duration, NULL);
}

static int vfs_result(int result) {
  if (result < 0) {
    errno = -result;
    return -1;
  }
  return result;
}

static int vfs_invoke(uint32_t operation, vfs_syscall_request_t *request) {
  request->size = sizeof(*request);
  return vfs_syscall(operation, request);
}

int open(const char *path, int flags, ...) {
  if (path == NULL) {
    errno = EINVAL;
    return -1;
  }
  uint32_t vfs_flags;
  if ((flags & O_ACCMODE) == O_RDONLY) {
    vfs_flags = VFS_OPEN_READ;
  } else if ((flags & O_ACCMODE) == O_WRONLY) {
    vfs_flags = VFS_OPEN_WRITE;
  } else if ((flags & O_ACCMODE) == O_RDWR) {
    vfs_flags = VFS_OPEN_READ | VFS_OPEN_WRITE;
  } else {
    errno = EINVAL;
    return -1;
  }
  if ((flags & O_CREAT) != 0) {
    vfs_flags |= VFS_OPEN_CREATE;
  }
  if ((flags & O_EXCL) != 0) {
    vfs_flags |= VFS_OPEN_EXCLUSIVE;
  }
  if ((flags & O_TRUNC) != 0) {
    vfs_flags |= VFS_OPEN_TRUNCATE;
  }
  if ((flags & O_APPEND) != 0) {
    vfs_flags |= VFS_OPEN_APPEND;
  }
  if (flags & O_DIRECTORY)
    vfs_flags |= VFS_OPEN_DIRECTORY;
  vfs_syscall_request_t request = {0};
  request.arguments.open.path = (uintptr_t)path;
  request.arguments.open.flags = vfs_flags;
  return vfs_result(vfs_invoke(VFS_SYSCALL_OPEN, &request));
}

int close(int descriptor) {
  if (descriptor >= 0 && descriptor <= 2) {
    return 0;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.descriptor.descriptor = descriptor;
  return vfs_result(vfs_invoke(VFS_SYSCALL_CLOSE, &request));
}

ssize_t write(int descriptor, const void *buffer, size_t count) {
  if (descriptor == 1 || descriptor == 2) {
    const char *bytes = buffer;
    for (size_t index = 0; index < count; index++) {
      putch(bytes[index]);
    }
    return count;
  }
  if (descriptor < 3 || (count != 0 && buffer == NULL)) {
    errno = EBADF;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.io.descriptor = descriptor;
  request.arguments.io.buffer = (uintptr_t)buffer;
  request.arguments.io.length = count;
  return vfs_result(vfs_invoke(VFS_SYSCALL_WRITE, &request));
}

ssize_t read(int descriptor, void *buffer, size_t count) {
  if (descriptor == 0) {
    scan(buffer, count);
    return count;
  }
  if (descriptor < 3 || (count != 0 && buffer == NULL)) {
    errno = EBADF;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.io.descriptor = descriptor;
  request.arguments.io.buffer = (uintptr_t)buffer;
  request.arguments.io.length = count;
  return vfs_result(vfs_invoke(VFS_SYSCALL_READ, &request));
}

off_t lseek(int descriptor, off_t offset, int whence) {
  if (descriptor < 3) {
    errno = EINVAL;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.seek.descriptor = descriptor;
  request.arguments.seek.offset = offset;
  request.arguments.seek.whence = whence;
  return vfs_result(vfs_invoke(VFS_SYSCALL_SEEK, &request));
}

int fsync(int descriptor) {
  vfs_syscall_request_t request = {0};
  request.arguments.descriptor.descriptor = descriptor;
  return vfs_result(vfs_invoke(VFS_SYSCALL_SYNC, &request));
}

int ftruncate(int descriptor, off_t length) {
  if (length < 0 || (uint64_t)length > UINT32_MAX) {
    errno = length < 0 ? EINVAL : EOVERFLOW;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.truncate.descriptor = descriptor;
  request.arguments.truncate.length = length;
  return vfs_result(vfs_invoke(VFS_SYSCALL_TRUNCATE, &request));
}

static int stat_from_vfs(const vfs_file_stat_t *source,
                         struct stat *destination) {
  if (source->size > __LONG_MAX__) {
    errno = EOVERFLOW;
    return -1;
  }
  memset(destination, 0, sizeof(*destination));
  destination->st_mode = (source->type == FILE_DIRECTORY ? S_IFDIR : S_IFREG) |
                         (source->attributes == RDO ? 0555 : 0777);
  destination->st_dev = source->device;
  destination->st_ino = source->inode;
  destination->st_nlink = 1;
  destination->st_size = source->size;
  destination->st_mtime = source->modified_time;
  destination->st_blksize = VM_PAGE_SIZE;
  destination->st_blocks = ((uint64_t)source->size + 511) / 512;
  return 0;
}

int stat(const char *path, struct stat *status) {
  if (path == NULL || status == NULL) {
    errno = EINVAL;
    return -1;
  }
  vfs_file_stat_t vfs_status;
  vfs_syscall_request_t request = {0};
  request.arguments.stat.path = (uintptr_t)path;
  request.arguments.stat.status = (uintptr_t)&vfs_status;
  int result = vfs_result(vfs_invoke(VFS_SYSCALL_STAT, &request));
  if (result == 0) {
    return stat_from_vfs(&vfs_status, status);
  }
  return result;
}

int fstat(int descriptor, struct stat *status) {
  if (status == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (descriptor >= 0 && descriptor <= 2) {
    memset(status, 0, sizeof(*status));
    status->st_mode = S_IFCHR | 0666;
    status->st_nlink = 1;
    status->st_blksize = VM_PAGE_SIZE;
    return 0;
  }
  vfs_file_stat_t vfs_status;
  vfs_syscall_request_t request = {0};
  request.arguments.fstat.descriptor = descriptor;
  request.arguments.fstat.status = (uintptr_t)&vfs_status;
  int result = vfs_result(vfs_invoke(VFS_SYSCALL_FSTAT, &request));
  if (result == 0) {
    return stat_from_vfs(&vfs_status, status);
  }
  return result;
}

int lstat(const char *path, struct stat *status) { return stat(path, status); }
int access(const char *path, int mode) {
  if (mode & ~(R_OK | W_OK | X_OK)) {
    errno = EINVAL;
    return -1;
  }
  struct stat status;
  if (stat(path, &status))
    return -1;
  if (((mode & R_OK) && !(status.st_mode & 0444)) ||
      ((mode & W_OK) && !(status.st_mode & 0222)) ||
      ((mode & X_OK) && !(status.st_mode & 0111))) {
    errno = EACCES;
    return -1;
  }
  return 0;
}

char *realpath(const char *path, char *resolved) {
  if (!path) {
    errno = EINVAL;
    return NULL;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.canonical.path = (uintptr_t)path;
  for (;;) {
    int required = vfs_result(vfs_invoke(VFS_SYSCALL_REALPATH, &request));
    if (required < 0)
      return NULL;
    size_t capacity = resolved ? PATH_MAX : (size_t)required + 1;
    char *buffer = resolved ? resolved : malloc(capacity);
    if (!buffer)
      return NULL;
    request.arguments.canonical.buffer = (uintptr_t)buffer;
    request.arguments.canonical.capacity = capacity;
    int status = vfs_result(vfs_invoke(VFS_SYSCALL_REALPATH, &request));
    if (status >= 0)
      return buffer;
    if (!resolved)
      free(buffer);
    if (resolved || errno != EOVERFLOW)
      return NULL;
    request.arguments.canonical.buffer = 0;
    request.arguments.canonical.capacity = 0;
  }
}

int mkdir(const char *path, ...) {
  if (path == NULL) {
    errno = EINVAL;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.path.path = (uintptr_t)path;
  return vfs_result(vfs_invoke(VFS_SYSCALL_MKDIR, &request));
}

int rmdir(const char *path) {
  vfs_syscall_request_t request = {0};
  request.arguments.path.path = (uintptr_t)path;
  return vfs_result(vfs_invoke(VFS_SYSCALL_RMDIR, &request));
}

int chdir(const char *path) {
  vfs_syscall_request_t request = {0};
  request.arguments.path.path = (uintptr_t)path;
  return vfs_result(vfs_invoke(VFS_SYSCALL_CHDIR, &request));
}

int vfs_check_mount(uint8_t drive) {
  vfs_syscall_request_t request = {0};
  request.arguments.mount.drive = drive;
  return vfs_invoke(VFS_SYSCALL_MOUNT_CHECK, &request) > 0;
}

int vfs_mount(uint8_t disk, uint8_t drive) {
  vfs_syscall_request_t request = {0};
  request.arguments.mount.disk = disk;
  request.arguments.mount.drive = drive;
  return vfs_invoke(VFS_SYSCALL_MOUNT, &request) > 0;
}

int vfs_unmount_disk(uint8_t drive) {
  vfs_syscall_request_t request = {0};
  request.arguments.mount.drive = drive;
  return vfs_invoke(VFS_SYSCALL_UNMOUNT, &request) > 0;
}

int vfs_change_disk(uint8_t drive) {
  vfs_syscall_request_t request = {0};
  request.arguments.mount.drive = drive;
  return vfs_invoke(VFS_SYSCALL_CHANGE_DRIVE, &request) == 0;
}

int api_current_drive(void) {
  vfs_syscall_request_t request = {0};
  return vfs_invoke(VFS_SYSCALL_CURRENT_DRIVE, &request);
}

int format(unsigned disk, char *filesystem) {
  vfs_syscall_request_t request = {0};
  request.arguments.format.disk = disk;
  request.arguments.format.filesystem = (uintptr_t)filesystem;
  return vfs_result(vfs_invoke(VFS_SYSCALL_FORMAT, &request));
}

int Copy(char *source, char *destination) {
  int input = open(source, O_RDONLY);
  if (input < 0) {
    return -1;
  }
  int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0);
  if (output < 0) {
    close(input);
    return -1;
  }
  unsigned char buffer[4096];
  int result = 0;
  for (;;) {
    ssize_t count = read(input, buffer, sizeof(buffer));
    if (count < 0) {
      result = -1;
      break;
    }
    if (count == 0) {
      break;
    }
    size_t written = 0;
    while (written < (size_t)count) {
      ssize_t chunk = write(output, buffer + written, count - written);
      if (chunk <= 0) {
        result = -1;
        break;
      }
      written += chunk;
    }
    if (result != 0) {
      break;
    }
  }
  if (close(input) != 0 || close(output) != 0) {
    result = -1;
  }
  return result;
}
