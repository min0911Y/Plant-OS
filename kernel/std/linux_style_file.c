#include <dos.h>
#include <fcntl.h>

static uint32_t kernel_open_flags(int flags) {
  uint32_t result;
  if ((flags & O_ACCMODE) == O_WRONLY) {
    result = VFS_OPEN_WRITE;
  } else if ((flags & O_ACCMODE) == O_RDWR) {
    result = VFS_OPEN_READ | VFS_OPEN_WRITE;
  } else {
    result = VFS_OPEN_READ;
  }
  if ((flags & O_CREAT) != 0) {
    result |= VFS_OPEN_CREATE;
  }
  if ((flags & O_EXCL) != 0) {
    result |= VFS_OPEN_EXCLUSIVE;
  }
  if ((flags & O_TRUNC) != 0) {
    result |= VFS_OPEN_TRUNCATE;
  }
  if ((flags & O_APPEND) != 0) {
    result |= VFS_OPEN_APPEND;
  }
  return result;
}

int open(const char *pathname, int flags, unsigned int mode) {
  (void)mode;
  if (current_task() == NULL || current_task()->fs_context == NULL) {
    return -1;
  }
  int descriptor = vfs_fd_open(current_task()->fs_context, pathname,
                               kernel_open_flags(flags));
  return descriptor < 0 ? -1 : descriptor;
}

int close(int descriptor) {
  return vfs_fd_close(current_task()->fs_context, descriptor) < 0 ? -1 : 0;
}

unsigned int read(int descriptor, void *buffer, unsigned int count) {
  int result =
      vfs_fd_read(current_task()->fs_context, descriptor, buffer, count);
  return result < 0 ? (unsigned int)-1 : (unsigned int)result;
}

unsigned int write(int descriptor, const void *buffer, unsigned int count) {
  int result =
      vfs_fd_write(current_task()->fs_context, descriptor, buffer, count);
  return result < 0 ? (unsigned int)-1 : (unsigned int)result;
}

int lseek(int descriptor, int offset, int whence) {
  return vfs_fd_seek(current_task()->fs_context, descriptor, offset, whence);
}
