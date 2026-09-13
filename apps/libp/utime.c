#include <errno.h>
#include <utime.h>

int utime(const char *path, const struct utimbuf *times) {
  (void)path;
  (void)times;
  errno = ENOTSUP;
  return -1;
}
