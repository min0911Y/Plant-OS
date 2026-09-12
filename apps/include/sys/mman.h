#ifndef PLANT_SYS_MMAN_H
#define PLANT_SYS_MMAN_H

#include <sys/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_NORESERVE 0x4000
#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4
#define MADV_DONTNEED 4
#define MAP_FAILED ((void *)-1)
/* Partial POSIX mapping interface used by the HotSpot port and the runtime.
 *
 * Supported: MAP_PRIVATE and MAP_SHARED over both anonymous memory and a
 * descriptor, PROT_NONE/R/RW/RX, MAP_FIXED, MAP_FIXED_NOREPLACE, MADV_DONTNEED
 * and msync. Rejected with ENOTSUP: MAP_SHARED anonymous memory, any other
 * advice or flag, and file mappings whose range is not inside the page-aligned
 * file length. W+X is rejected with EACCES. Anonymous mappings never touch the
 * descriptor or offset; a file mapping neither consumes nor moves the
 * descriptor offset and stays valid after it is closed.
 *
 * A file mapping reads its pages through the VFS cache, so ordinary reads and
 * writes of the same file observe mapped stores and vice versa. Shared writable
 * mappings write back through msync and fsync; while one exists the file cannot
 * be truncated or unlinked (EBUSY). MS_ASYNC and MS_INVALIDATE are accepted but
 * currently perform the same synchronous writeback as MS_SYNC. */
#ifdef __cplusplus
extern "C" {
#endif
void *mmap(void *address, size_t length, int protection, int flags,
           int descriptor, off_t offset);
int msync(void *address, size_t length, int flags);
int madvise(void *address, size_t length, int advice);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);
#ifdef __cplusplus
}
#endif
#endif
