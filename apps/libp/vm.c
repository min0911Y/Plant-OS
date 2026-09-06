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

void *vm_map(void *address, size_t length) {
  intptr_t result =
      vm_invoke(VM_MAP, (vm_request_t){(uintptr_t)address, length, 0});
  return result == -1 ? NULL : (void *)result;
}
int vm_protect(void *address, size_t length, unsigned protection) {
  return vm_invoke(VM_PROTECT,
                   (vm_request_t){(uintptr_t)address, length, protection});
}
int vm_unmap(void *address, size_t length) {
  return vm_invoke(VM_UNMAP, (vm_request_t){(uintptr_t)address, length, 0});
}
