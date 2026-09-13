#ifndef PLANT_SYS_UIO_H
#define PLANT_SYS_UIO_H

#include <sys/types.h>

#define IOV_MAX 1024

struct iovec {
  void *iov_base;
  size_t iov_len;
};

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int descriptor, const struct iovec *vectors, int count);
ssize_t writev(int descriptor, const struct iovec *vectors, int count);

#ifdef __cplusplus
}
#endif

#endif
