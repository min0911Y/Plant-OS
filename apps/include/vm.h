#ifndef PLANT_VM_H
#define PLANT_VM_H

#include <ctypes.h>

enum { SYSCALL_VM = 0x66, VM_PAGE_SIZE = 4096 };
enum { VM_MAP, VM_PROTECT, VM_UNMAP, VM_OPERATION_COUNT };
enum { VM_READ = 1, VM_WRITE = 2, VM_EXEC = 4 };

typedef struct {
  uintptr_t address;
  size_t length;
  uint32_t protection;
} vm_request_t;

#ifdef __cplusplus
extern "C" {
#endif
/* Anonymous private mappings, independent of the heap. A non-NULL address is
 * exact and never replaces an existing mapping. Lengths must be page aligned.
 * New mappings are zeroed and read/write. Errors set errno. */
void *vm_map(void *address, size_t length);
int vm_protect(void *address, size_t length, unsigned protection);
int vm_unmap(void *address, size_t length);
#ifdef __cplusplus
}
#endif
#endif
