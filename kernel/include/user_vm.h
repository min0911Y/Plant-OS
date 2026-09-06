#ifndef KERNEL_USER_VM_H
#define KERNEL_USER_VM_H

#include "../../apps/include/vm.h"

enum {
  VM_ERROR_NOMEM = -12,
  VM_ERROR_FAULT = -14,
  VM_ERROR_EXISTS = -17,
  VM_ERROR_INVALID = -22,
};

unsigned arch_user_page_flags(uintptr_t address);
bool arch_user_map_zero(uintptr_t address);
uintptr_t arch_user_find_free(uintptr_t lower, uintptr_t upper, size_t size);
bool arch_user_protect(uintptr_t address, size_t size, unsigned protection);
bool arch_user_unmap(uintptr_t address, size_t size);
bool user_vm_range_free(uintptr_t address, size_t length);
bool user_vm_prepare_write(uintptr_t address, size_t length);
intptr_t user_vm_operation(unsigned operation, const vm_request_t *request);

#endif
