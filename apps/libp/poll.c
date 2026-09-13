#include <errno.h>
#include <poll.h>
#include <socket.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  if (nfds != 0 && fds == NULL) {
    errno = EFAULT;
    return -1;
  }
  int ready = 0;
  for (nfds_t index = 0; index < nfds; index++) {
    struct pollfd *descriptor = &fds[index];
    descriptor->revents = 0;
    if (descriptor->fd < 0) {
      continue;
    }

    uint32_t available = 0;
    int result = socket_bytes_available(descriptor->fd, &available);
    if (result == 0) {
      if ((descriptor->events & POLLIN) != 0 && available != 0) {
        descriptor->revents |= POLLIN;
      }
      if ((descriptor->events & POLLOUT) != 0) {
        descriptor->revents |= POLLOUT;
      }
    } else if (result == SOCKET_ERR_NOENT) {
      descriptor->revents |= POLLNVAL;
    } else {
      descriptor->revents |= POLLERR;
    }
    if (descriptor->revents != 0) {
      ready++;
    }
  }
  (void)timeout;
  return ready;
}
