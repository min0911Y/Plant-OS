#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <syscall.h>

int poll(struct pollfd *fds, nfds_t count, int timeout) {
  if (count > INT_MAX) {
    errno = EINVAL;
    return -1;
  }
  if (count && !fds) {
    errno = EFAULT;
    return -1;
  }
  vfs_syscall_request_t request = {.size = sizeof(request)};
  request.arguments.poll.fds = (uintptr_t)fds;
  request.arguments.poll.count = count;
  request.arguments.poll.timeout = timeout;
  int result = vfs_syscall(VFS_SYSCALL_POLL, &request);
  if (result < 0) {
    errno = -result;
    return -1;
  }
  return result;
}
