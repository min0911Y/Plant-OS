#ifndef PLANT_VM_H
#define PLANT_VM_H

#include <ctypes.h>

enum { SYSCALL_VM = 0x66, VM_PAGE_SIZE = 4096 };
enum {
  VM_MAP,
  VM_PROTECT,
  VM_UNMAP,
  VM_DISCARD,
  VM_SYNC,
  VM_ALIAS,
  VM_OPERATION_COUNT
};
enum { VM_REPLACE = 1, VM_FILE = 2, VM_SHARED = 4 };
enum { VM_READ = 1, VM_WRITE = 2, VM_EXEC = 4 };

/* One request per operation. i386 is 36 bytes, x86_64 is 48; the kernel rejects
 * a caller whose compiled size differs, so both sides share this layout. */
typedef struct {
  uintptr_t address;
  size_t length;
  size_t alignment;
  uint32_t protection;
  uint32_t flags;
  uint64_t offset;
  int32_t descriptor;
  uint32_t reserved;
} vm_request_t;

#ifdef __cplusplus
static_assert(sizeof(vm_request_t) == 3 * sizeof(uintptr_t) + 24,
              "VM request ABI");
extern "C" {
#endif
#ifndef __cplusplus
_Static_assert(sizeof(vm_request_t) == 3 * sizeof(uintptr_t) + 24,
               "VM request ABI");
#endif
/* Address space control shared by the heap, GC, stacks and the code cache. A
 * non-NULL address is exact and never replaces an existing mapping. Lengths are
 * page aligned and vm_map_aligned accepts a power-of-two alignment of at least
 * one page. New mappings are lazy zeroed with the requested protection, minus
 * VM_WRITE alone; PROT_NONE retains the address and its contents. Errors set
 * errno and return NULL or -1.
 *
 * vm_map          reserve RW anonymous memory, independent of the heap.
 * vm_map_aligned  as above with explicit protection, alignment and VM_REPLACE.
 * vm_map_file     back the range with an open descriptor at a page offset.
 * vm_map_alias    map a private RW alias into an empty or PROT_NONE range.
 * vm_protect      change access rights without disturbing contents.
 * vm_discard      drop contents, keeping the address and its protection.
 * vm_sync         write shared file contents, and their metadata, to the disk.
 * vm_unmap        release the address, its contents and any file backing. */
void *vm_map(void *address, size_t length);
void *vm_map_aligned(void *address, size_t length, size_t alignment,
                     unsigned protection, unsigned flags);
void *vm_map_file(void *address, size_t length, unsigned protection,
                  unsigned flags, int descriptor, uint64_t offset);
void *vm_map_alias(void *source, void *address, size_t length);
int vm_sync(void *address, size_t length);
int vm_discard(void *address, size_t length);
int vm_protect(void *address, size_t length, unsigned protection);
int vm_unmap(void *address, size_t length);
#ifdef __cplusplus
}
#endif
#endif
