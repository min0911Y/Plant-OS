#include <sys/xattr.h>

#include <errno.h>

ssize_t fgetxattr(int descriptor, const char *name, void *value, size_t size,
                  uint32_t position, int options) {
  (void)descriptor;
  (void)name;
  (void)value;
  (void)size;
  (void)position;
  (void)options;
  errno = ENOTSUP;
  return -1;
}

int fsetxattr(int descriptor, const char *name, const void *value, size_t size,
              uint32_t position, int options) {
  (void)descriptor;
  (void)name;
  (void)value;
  (void)size;
  (void)position;
  (void)options;
  errno = ENOTSUP;
  return -1;
}

int fremovexattr(int descriptor, const char *name, int options) {
  (void)descriptor;
  (void)name;
  (void)options;
  errno = ENOTSUP;
  return -1;
}

ssize_t flistxattr(int descriptor, char *list, size_t size, int options) {
  (void)descriptor;
  (void)list;
  (void)size;
  (void)options;
  errno = ENOTSUP;
  return -1;
}
