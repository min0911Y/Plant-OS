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
#define MAP_FAILED ((void *)-1)
#ifdef __cplusplus
extern "C" {
#endif
void *mmap(void *address, size_t length, int protection, int flags,
           int descriptor, off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);
#ifdef __cplusplus
}
#endif
#endif
