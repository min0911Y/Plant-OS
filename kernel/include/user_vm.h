#ifndef KERNEL_USER_VM_H
#define KERNEL_USER_VM_H

#include "../../apps/include/vm.h"

enum {
  VM_ERROR_NOMEM = -12,
  VM_ERROR_FAULT = -14,
  VM_ERROR_EXISTS = -17,
  VM_ERROR_INVALID = -22,
};

/* The lock covers the whole of one VM operation; nested helpers must not take
 * it again. Release is available unlocked for callers that already hold it. */
void user_vm_lock(void);
void user_vm_unlock(void);
void user_vm_release_locked(arch_address_space_t root);
bool user_vm_file_page(uintptr_t address);
bool user_vm_clone(arch_address_space_t source, arch_address_space_t target);
void user_vm_release(arch_address_space_t root);
intptr_t user_vm_apply(unsigned operation, uintptr_t address,
                       const vm_request_t *request);
bool arch_user_map_pages(uintptr_t address, size_t size, unsigned protection,
                         bool replace, void *const *backing, bool shared);
enum { VM_MAPPED = 8 };
bool arch_user_map(uintptr_t address, size_t size, unsigned protection,
                   bool replace);
bool arch_user_discard(uintptr_t address, size_t size);
bool arch_user_validate(uintptr_t address, size_t size);
unsigned arch_user_page_flags(uintptr_t address);
bool arch_user_map_zero(uintptr_t address);
uintptr_t arch_user_find_free(uintptr_t lower, uintptr_t upper, size_t size,
                              size_t alignment);
bool arch_user_protect(uintptr_t address, size_t size, unsigned protection);
bool arch_user_unmap(uintptr_t address, size_t size);
bool user_vm_range_free(uintptr_t address, size_t length);
bool user_vm_readable(uintptr_t address, size_t length);
bool user_vm_copy_from(void *destination, uintptr_t address, size_t length);
bool user_vm_copy_to(uintptr_t address, const void *source, size_t length);
bool user_vm_prepare_write(uintptr_t address, size_t length);
intptr_t user_vm_operation(unsigned operation, const vm_request_t *request);

#endif
