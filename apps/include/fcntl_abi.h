#ifndef PLOS_FCNTL_ABI_H
#define PLOS_FCNTL_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define PLOS_STATIC_ASSERT static_assert
#else
#define PLOS_STATIC_ASSERT _Static_assert
#endif

/*
 * The record-lock ABI is shared by the freestanding C library, OpenJDK's
 * native NIO code and the kernel.  Keep the offsets independent of the host
 * libc and of the width of off_t used by unrelated POSIX APIs.
 */
struct flock {
  int16_t l_type;
  int16_t l_whence;
  int64_t l_start;
  int64_t l_len;
  int32_t l_pid;
};

struct flock64 {
  int16_t l_type;
  int16_t l_whence;
  int64_t l_start;
  int64_t l_len;
  int32_t l_pid;
};

#if __SIZEOF_POINTER__ == 8
PLOS_STATIC_ASSERT(offsetof(struct flock, l_start) == 8,
                   "x86_64 flock start offset");
PLOS_STATIC_ASSERT(offsetof(struct flock, l_len) == 16,
                   "x86_64 flock length offset");
PLOS_STATIC_ASSERT(offsetof(struct flock, l_pid) == 24,
                   "x86_64 flock pid offset");
PLOS_STATIC_ASSERT(sizeof(struct flock) == 32, "x86_64 flock size");
#else
PLOS_STATIC_ASSERT(offsetof(struct flock, l_start) == 4,
                   "i386 flock start offset");
PLOS_STATIC_ASSERT(offsetof(struct flock, l_len) == 12,
                   "i386 flock length offset");
PLOS_STATIC_ASSERT(offsetof(struct flock, l_pid) == 20,
                   "i386 flock pid offset");
PLOS_STATIC_ASSERT(sizeof(struct flock) == 24, "i386 flock size");
#endif

PLOS_STATIC_ASSERT(sizeof(struct flock64) == sizeof(struct flock),
                   "flock and flock64 ABI must match");

#undef PLOS_STATIC_ASSERT

#endif
