#ifndef PLANT_SYS_XATTR_H
#define PLANT_SYS_XATTR_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t fgetxattr(int descriptor, const char *name, void *value, size_t size,
                  uint32_t position, int options);
int fsetxattr(int descriptor, const char *name, const void *value, size_t size,
              uint32_t position, int options);
int fremovexattr(int descriptor, const char *name, int options);
ssize_t flistxattr(int descriptor, char *list, size_t size, int options);

#ifdef __cplusplus
}
#endif

#endif
