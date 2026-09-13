#include <errno.h>
#include <termios.h>

int tcgetattr(int descriptor, struct termios *attributes) {
  if (descriptor < 0) {
    errno = EBADF;
  } else if (attributes == NULL) {
    errno = EFAULT;
  } else {
    errno = ENOTTY;
  }
  return -1;
}

int tcsetattr(int descriptor, int actions, const struct termios *attributes) {
  (void)actions;
  if (descriptor < 0) {
    errno = EBADF;
  } else if (attributes == NULL) {
    errno = EFAULT;
  } else {
    errno = ENOTTY;
  }
  return -1;
}
