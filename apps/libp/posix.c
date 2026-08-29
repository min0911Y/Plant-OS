#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>

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
  vfs_syscall_request_t request = {0};
  request.arguments.open.path = (uint32_t)(uintptr_t)path;
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
  request.arguments.io.buffer = (uint32_t)(uintptr_t)buffer;
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
  request.arguments.io.buffer = (uint32_t)(uintptr_t)buffer;
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

static void stat_from_vfs(const vfs_file_stat_t *source,
                          struct stat *destination) {
  memset(destination, 0, sizeof(*destination));
  destination->st_mode = source->type == DIR ? S_IFDIR : S_IFREG;
  destination->st_size = source->size;
  destination->st_mtime = source->modified_time;
}

int stat(const char *path, struct stat *status) {
  if (path == NULL || status == NULL) {
    errno = EINVAL;
    return -1;
  }
  vfs_file_stat_t vfs_status;
  vfs_syscall_request_t request = {0};
  request.arguments.stat.path = (uint32_t)(uintptr_t)path;
  request.arguments.stat.status = (uint32_t)(uintptr_t)&vfs_status;
  int result = vfs_result(vfs_invoke(VFS_SYSCALL_STAT, &request));
  if (result == 0) {
    stat_from_vfs(&vfs_status, status);
  }
  return result;
}

int fstat(int descriptor, struct stat *status) {
  if (status == NULL) {
    errno = EINVAL;
    return -1;
  }
  vfs_file_stat_t vfs_status;
  vfs_syscall_request_t request = {0};
  request.arguments.fstat.descriptor = descriptor;
  request.arguments.fstat.status = (uint32_t)(uintptr_t)&vfs_status;
  int result = vfs_result(vfs_invoke(VFS_SYSCALL_FSTAT, &request));
  if (result == 0) {
    stat_from_vfs(&vfs_status, status);
  }
  return result;
}

int mkdir(const char *path, ...) {
  if (path == NULL) {
    errno = EINVAL;
    return -1;
  }
  vfs_syscall_request_t request = {0};
  request.arguments.path.path = (uint32_t)(uintptr_t)path;
  return vfs_result(vfs_invoke(VFS_SYSCALL_MKDIR, &request));
}

int rmdir(const char *path) {
  vfs_syscall_request_t request = {0};
  request.arguments.path.path = (uint32_t)(uintptr_t)path;
  return vfs_result(vfs_invoke(VFS_SYSCALL_RMDIR, &request));
}

int chdir(const char *path) {
  vfs_syscall_request_t request = {0};
  request.arguments.path.path = (uint32_t)(uintptr_t)path;
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
  request.arguments.format.filesystem = (uint32_t)(uintptr_t)filesystem;
  return vfs_invoke(VFS_SYSCALL_FORMAT, &request) > 0;
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
