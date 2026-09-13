#include <errno.h>
#include <vm.h>

intptr_t libp_syscall3(uintptr_t number, uintptr_t a, uintptr_t b, uintptr_t c);

static intptr_t vm_invoke(unsigned operation, vm_request_t request) {
  intptr_t result = libp_syscall3(SYSCALL_VM, operation, (uintptr_t)&request,
                                  sizeof(request));
  /* Successful i386 mappings may have the sign bit set. */
  if ((uintptr_t)result >= (uintptr_t)-4095) {
    errno = -result;
    return -1;
  }
  return result;
}

void *vm_map_aligned(void *address, size_t length, size_t alignment,
                     unsigned protection, unsigned flags) {
  intptr_t result =
      vm_invoke(VM_MAP, (vm_request_t){.address = (uintptr_t)address,
                                       .length = length,
                                       .alignment = alignment,
                                       .protection = protection,
                                       .flags = flags});
  return result == -1 ? NULL : (void *)result;
}
void *vm_map(void *address, size_t length) {
  return vm_map_aligned(address, length, VM_PAGE_SIZE, VM_READ | VM_WRITE, 0);
}
void *vm_map_alias(void *source, void *address, size_t length) {
  intptr_t result =
      vm_invoke(VM_ALIAS, (vm_request_t){.address = (uintptr_t)address,
                                         .length = length,
                                         .offset = (uintptr_t)source,
                                         .protection = VM_READ | VM_WRITE});
  return result == -1 ? NULL : (void *)result;
}
int vm_protect(void *address, size_t length, unsigned protection) {
  return vm_invoke(VM_PROTECT, (vm_request_t){.address = (uintptr_t)address,
                                              .length = length,
                                              .protection = protection});
}
int vm_unmap(void *address, size_t length) {
  return vm_invoke(VM_UNMAP, (vm_request_t){.address = (uintptr_t)address,
                                            .length = length});
}
int vm_discard(void *address, size_t length) {
  return vm_invoke(VM_DISCARD, (vm_request_t){.address = (uintptr_t)address,
                                              .length = length});
}

void *vm_map_file(void *address, size_t length, unsigned protection,
                  unsigned flags, int descriptor, uint64_t offset) {
  intptr_t result =
      vm_invoke(VM_MAP, (vm_request_t){.address = (uintptr_t)address,
                                       .length = length,
                                       .alignment = VM_PAGE_SIZE,
                                       .protection = protection,
                                       .flags = flags | VM_FILE,
                                       .descriptor = descriptor,
                                       .offset = offset});
  return result == -1 ? NULL : (void *)result;
}
int vm_sync(void *address, size_t length) {
  return vm_invoke(
      VM_SYNC, (vm_request_t){.address = (uintptr_t)address, .length = length});
}
