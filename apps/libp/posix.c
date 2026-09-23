#include "runtime_lifecycle.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <socket.h>
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

static char *root_group_members[] = {NULL};
static struct group root_group = {"root", "", 0, root_group_members};

struct group *getgrgid(gid_t gid) { return gid == 0 ? &root_group : NULL; }

struct group *getgrnam(const char *name) {
  return name && !strcmp(name, root_group.gr_name) ? &root_group : NULL;
}

int getgrnam_r(const char *name, struct group *entry, char *buffer,
               size_t size, struct group **result) {
  *result = NULL;
  struct group *group = getgrnam(name);
  if (!group)
    return 0;

  size_t alignment = _Alignof(char *);
  size_t padding = (-(uintptr_t)buffer) & (alignment - 1);
  size_t name_size = strlen(group->gr_name) + 1;
  size_t password_size = strlen(group->gr_passwd) + 1;
  size_t required = padding + sizeof(char *) + name_size + password_size;
  if (size < required)
    return ERANGE;

  char **members = (char **)(buffer + padding);
  char *strings = (char *)(members + 1);
  *entry = *group;
  entry->gr_mem = members;
  members[0] = NULL;
  entry->gr_name = strings;
  memcpy(strings, group->gr_name, name_size);
  strings += name_size;
  entry->gr_passwd = strings;
  memcpy(strings, group->gr_passwd, password_size);
  *result = entry;
  return 0;
}

int getgrgid_r(gid_t gid, struct group *entry, char *buffer, size_t size,
               struct group **result) {
  if (gid) {
    *result = NULL;
    return 0;
  }
  return getgrnam_r("root", entry, buffer, size, result);
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
  case _SC_AVPHYS_PAGES: {
    size_t total = mem_total() / VM_PAGE_SIZE;
    size_t used = mem_used();
    return used < total ? (long)(total - used) : 0;
  }
  case _SC_OPEN_MAX:
    return __INT_MAX__;
  case _SC_GETPW_R_SIZE_MAX:
  case _SC_GETGR_R_SIZE_MAX:
    return 1024;
  case _SC_IOV_MAX:
    return IOV_MAX;
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
  if (socket_handle_is_tagged(descriptor)) {
    errno = ENOTTY;
    return 0;
  }
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
  if (flags & O_NONBLOCK)
    vfs_flags |= VFS_OPEN_NONBLOCK;
  if (flags & O_CLOEXEC)
    vfs_flags |= VFS_OPEN_CLOEXEC;
  vfs_syscall_request_t request = {0};
  request.arguments.open.path = (uintptr_t)path;
  request.arguments.open.flags = vfs_flags;
  return vfs_result(vfs_invoke(VFS_SYSCALL_OPEN, &request));
}

int fcntl(int descriptor, int command, ...) {
  if (descriptor < 0) {
    errno = EBADF;
    return -1;
  }

  uintptr_t argument = 0;
  if (command == F_SETFL || command == F_SETFD) {
    va_list arguments;
    va_start(arguments, command);
    argument = (uintptr_t)va_arg(arguments, int);
    va_end(arguments);
  } else if (command == F_GETLK || command == F_SETLK ||
             command == F_SETLKW) {
    va_list arguments;
    va_start(arguments, command);
    argument = (uintptr_t)va_arg(arguments, void *);
    va_end(arguments);
  }

  if (descriptor <= STDERR_FILENO) {
    switch (command) {
    case F_GETFL:
      return descriptor == STDIN_FILENO ? O_RDONLY : O_WRONLY;
    case F_SETFL:
    case F_GETFD:
    case F_SETFD:
      return 0;
    default:
      errno = EOPNOTSUPP;
      return -1;
    }
  }

  if (socket_handle_is_tagged(descriptor)) {
    int result;
    switch (command) {
    case F_GETFL:
      result = socket_get_flags(descriptor);
      break;
    case F_SETFL:
      result = socket_set_flags(descriptor, (int)argument);
      break;
    case F_GETFD:
      return 0;
    case F_SETFD:
      if (argument & ~((uintptr_t)FD_CLOEXEC)) {
        errno = EINVAL;
        return -1;
      }
      return 0;
    default:
      errno = ENOTSUP;
      return -1;
    }
    if (result < 0) {
      return -1;
    }
    return result;
  }

  vfs_syscall_request_t request = {0};
  request.arguments.fcntl.descriptor = descriptor;
  request.arguments.fcntl.command = command;
  request.arguments.fcntl.argument = argument;
  return vfs_result(vfs_invoke(VFS_SYSCALL_FCNTL, &request));
}

int close(int descriptor) {
  if (descriptor >= 0 && descriptor <= 2) {
    return 0;
  }
  if (socket_handle_is_tagged(descriptor)) {
    int result = socket_close(descriptor);
    if (result < 0) {
      return -1;
    }
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
  if (socket_handle_is_tagged(descriptor)) {
    int result = send(descriptor, buffer, (uint32_t)count, 0);
    if (result < 0) {
      return -1;
    }
    return result;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.io.descriptor = descriptor;
  request.arguments.io.buffer = (uintptr_t)buffer;
  request.arguments.io.length = count;
  int result = vfs_invoke(VFS_SYSCALL_WRITE, &request);
  if (result == -EPIPE)
    raise(SIGPIPE);
  return vfs_result(result);
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
  if (socket_handle_is_tagged(descriptor)) {
    int result = recv(descriptor, buffer, (uint32_t)count, 0);
    if (result < 0) {
      return -1;
    }
    return result;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.io.descriptor = descriptor;
  request.arguments.io.buffer = (uintptr_t)buffer;
  request.arguments.io.length = count;
  return vfs_result(vfs_invoke(VFS_SYSCALL_READ, &request));
}

static bool posix_descriptor_is_socket(int descriptor) {
  return socket_handle_is_tagged(descriptor);
}

ssize_t readv(int descriptor, const struct iovec *vectors, int count) {
  size_t total = 0;
  if (count < 0 || count > IOV_MAX || (count != 0 && vectors == NULL)) {
    errno = EINVAL;
    return -1;
  }
  for (int index = 0; index < count; index++) {
    if (vectors[index].iov_len != 0 && vectors[index].iov_base == NULL) {
      errno = EFAULT;
      return -1;
    }
    if (vectors[index].iov_len > SIZE_MAX - total) {
      errno = EOVERFLOW;
      return -1;
    }
    total += vectors[index].iov_len;
  }
  if (total > UINT32_MAX || total > (size_t)__INT_MAX__) {
    errno = EOVERFLOW;
    return -1;
  }
  if (count == 0 || total == 0) {
    return 0;
  }
  if (posix_descriptor_is_socket(descriptor)) {
    struct msghdr message = {0};
    message.msg_iov = (struct iovec *)vectors;
    message.msg_iovlen = (size_t)count;
    return recvmsg(descriptor, &message, 0);
  }

  size_t completed = 0;
  for (int index = 0; index < count; index++) {
    ssize_t result = read(descriptor, vectors[index].iov_base,
                          vectors[index].iov_len);
    if (result < 0) {
      return completed == 0 ? -1 : (ssize_t)completed;
    }
    completed += (size_t)result;
    if ((size_t)result < vectors[index].iov_len) {
      break;
    }
  }
  return (ssize_t)completed;
}

ssize_t writev(int descriptor, const struct iovec *vectors, int count) {
  size_t total = 0;
  if (count < 0 || count > IOV_MAX || (count != 0 && vectors == NULL)) {
    errno = EINVAL;
    return -1;
  }
  for (int index = 0; index < count; index++) {
    if (vectors[index].iov_len != 0 && vectors[index].iov_base == NULL) {
      errno = EFAULT;
      return -1;
    }
    if (vectors[index].iov_len > SIZE_MAX - total) {
      errno = EOVERFLOW;
      return -1;
    }
    total += vectors[index].iov_len;
  }
  if (total > UINT32_MAX || total > (size_t)__INT_MAX__) {
    errno = EOVERFLOW;
    return -1;
  }
  if (count == 0 || total == 0) {
    return 0;
  }
  if (posix_descriptor_is_socket(descriptor)) {
    struct msghdr message = {0};
    message.msg_iov = (struct iovec *)vectors;
    message.msg_iovlen = (size_t)count;
    return sendmsg(descriptor, &message, 0);
  }

  size_t completed = 0;
  for (int index = 0; index < count; index++) {
    ssize_t result = write(descriptor, vectors[index].iov_base,
                           vectors[index].iov_len);
    if (result < 0) {
      return completed == 0 ? -1 : (ssize_t)completed;
    }
    completed += (size_t)result;
    if ((size_t)result < vectors[index].iov_len) {
      break;
    }
  }
  return (ssize_t)completed;
}

ssize_t pwrite(int descriptor, const void *buffer, size_t count,
               off_t offset) {
  if (descriptor < 3 || (count != 0 && buffer == NULL)) {
    errno = EBADF;
    return -1;
  }
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }
  if (count > UINT32_MAX || (uint64_t)offset > UINT32_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  if (posix_descriptor_is_socket(descriptor)) {
    errno = ESPIPE;
    return -1;
  }

  off_t current = lseek(descriptor, 0, SEEK_CUR);
  if (current < 0) {
    return -1;
  }
  if (lseek(descriptor, offset, SEEK_SET) < 0) {
    return -1;
  }
  ssize_t result = write(descriptor, buffer, count);
  int saved_errno = errno;
  if (lseek(descriptor, current, SEEK_SET) < 0 && result >= 0) {
    errno = EIO;
    return -1;
  }
  if (result < 0) {
    errno = saved_errno;
  }
  return result;
}

ssize_t pread(int descriptor, void *buffer, size_t count, off_t offset) {
  if (descriptor < 3 || (count != 0 && buffer == NULL)) {
    errno = EBADF;
    return -1;
  }
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }
  if (count > UINT32_MAX || (uint64_t)offset > UINT32_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  if (posix_descriptor_is_socket(descriptor)) {
    errno = ESPIPE;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.positioned_io.descriptor = descriptor;
  request.arguments.positioned_io.buffer = (uintptr_t)buffer;
  request.arguments.positioned_io.length = count;
  request.arguments.positioned_io.offset = offset;
  return vfs_result(vfs_invoke(VFS_SYSCALL_PREAD, &request));
}

off_t lseek(int descriptor, off_t offset, int whence) {
  if (descriptor < 3) {
    errno = EINVAL;
    return -1;
  }
  if (posix_descriptor_is_socket(descriptor)) {
    errno = ESPIPE;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.seek.descriptor = descriptor;
  request.arguments.seek.offset = offset;
  request.arguments.seek.whence = whence;
  return vfs_result(vfs_invoke(VFS_SYSCALL_SEEK, &request));
}

int fsync(int descriptor) {
  if (posix_descriptor_is_socket(descriptor)) {
    errno = EINVAL;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.descriptor.descriptor = descriptor;
  return vfs_result(vfs_invoke(VFS_SYSCALL_SYNC, &request));
}

int ftruncate(int descriptor, off_t length) {
  if (length < 0 || (uint64_t)length > UINT32_MAX) {
    errno = length < 0 ? EINVAL : EOVERFLOW;
    return -1;
  }
  if (posix_descriptor_is_socket(descriptor)) {
    errno = EINVAL;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.truncate.descriptor = descriptor;
  request.arguments.truncate.length = length;
  return vfs_result(vfs_invoke(VFS_SYSCALL_TRUNCATE, &request));
}

int posix_fallocate(int descriptor, off_t offset, off_t length) {
  if (offset < 0 || length <= 0)
    return EINVAL;
  if (posix_descriptor_is_socket(descriptor))
    return ESPIPE;
  if ((uint64_t)offset > UINT32_MAX ||
      (uint64_t)length > UINT32_MAX - (uint64_t)offset)
    return EFBIG;
  struct stat status;
  if (fstat(descriptor, &status))
    return errno;
  off_t end = offset + length;
  return status.st_size >= end || ftruncate(descriptor, end) == 0 ? 0 : errno;
}

static int stat_from_vfs(const vfs_file_stat_t *source,
                         struct stat *destination) {
  if (source->size > __LONG_MAX__) {
    errno = EOVERFLOW;
    return -1;
  }
  memset(destination, 0, sizeof(*destination));
  destination->st_mode = (source->type == VFS_STAT_PIPE    ? S_IFIFO
                          : source->type == FILE_DIRECTORY ? S_IFDIR
                                                           : S_IFREG) |
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
  if (posix_descriptor_is_socket(descriptor)) {
    memset(status, 0, sizeof(*status));
    status->st_mode = S_IFSOCK | 0666;
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

int fchdir(int descriptor) {
  vfs_syscall_request_t request = {0};
  request.arguments.descriptor.descriptor = descriptor;
  return vfs_result(vfs_invoke(VFS_SYSCALL_FCHDIR, &request));
}

ssize_t getline(char **line, size_t *capacity, FILE *stream) {
  if (line == NULL || capacity == NULL || stream == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (*line == NULL || *capacity == 0) {
    *capacity = 128;
    *line = malloc(*capacity);
    if (*line == NULL) {
      *capacity = 0;
      errno = ENOMEM;
      return -1;
    }
  }

  size_t length = 0;
  for (;;) {
    int character = fgetc(stream);
    if (character == EOF) {
      if (length == 0) {
        return -1;
      }
      break;
    }
    if (length + 1 >= *capacity) {
      if (*capacity > SIZE_MAX / 2) {
        errno = EOVERFLOW;
        return -1;
      }
      size_t new_capacity = *capacity * 2;
      char *new_line = realloc(*line, new_capacity);
      if (new_line == NULL) {
        errno = ENOMEM;
        return -1;
      }
      *line = new_line;
      *capacity = new_capacity;
    }
    (*line)[length++] = (char)character;
    if (character == '\n') {
      break;
    }
  }
  (*line)[length] = '\0';
  return (ssize_t)length;
}

int vfs_check_mount(uint8_t drive) {
  vfs_syscall_request_t request = {0};
  request.arguments.mount.drive = drive;
  return vfs_invoke(VFS_SYSCALL_MOUNT_CHECK, &request) > 0;
}

int disk_list(disk_info_t *entries, uint32_t capacity) {
  vfs_syscall_request_t request = {0};
  request.arguments.cwd.buffer = (uintptr_t)entries;
  request.arguments.cwd.capacity = capacity;
  return vfs_result(vfs_invoke(VFS_SYSCALL_DISKS, &request));
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
  const size_t capacity = 128 * 1024;
  unsigned char *buffer = malloc(capacity);
  if (buffer == NULL) {
    close(input);
    return -1;
  }
  int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0);
  if (output < 0) {
    close(input);
    free(buffer);
    return -1;
  }
  int result = 0;
  for (;;) {
    ssize_t count = read(input, buffer, capacity);
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
  if (result == 0 && fsync(output) != 0) {
    result = -1;
  }
  int input_status = close(input);
  if (close(output) != 0 || input_status != 0) {
    result = -1;
  }
  free(buffer);
  return result;
}
