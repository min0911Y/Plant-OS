#include <dos.h>
#include <user_space.h>
#include <user_vm.h>

bool user_vm_range_free(uintptr_t address, size_t length) {
  return arch_user_find_free(address, address + length, length) == address;
}

intptr_t user_vm_operation(unsigned operation, const vm_request_t *request) {
  mtask *task = current_task();
  uintptr_t address = request->address;
  size_t length = request->length;
  unsigned protection = request->protection;
  if (!task->alloc_size || operation >= VM_OPERATION_COUNT || !length ||
      ((address | length) & (VM_PAGE_SIZE - 1)) ||
      (protection & ~(VM_READ | VM_WRITE | VM_EXEC)) ||
      (operation != VM_PROTECT && protection) ||
      (operation == VM_PROTECT &&
       (!(protection & VM_READ) ||
        (protection & (VM_WRITE | VM_EXEC)) == (VM_WRITE | VM_EXEC))))
    return VM_ERROR_INVALID;

  uintptr_t lower = task->alloc_addr + *task->alloc_size;
  if (lower < task->alloc_addr || lower >= USER_HEAP_END ||
      length > USER_HEAP_END - lower ||
      (operation == VM_MAP && length > memsize))
    return VM_ERROR_NOMEM;
  if (!address && operation == VM_MAP) {
    /* Search downward above the heap. Page tables are the allocation registry;
     * fork and task teardown therefore need no second list of mappings. */
    address = arch_user_find_free(lower, USER_HEAP_END, length);
    if (!address)
      return VM_ERROR_NOMEM;
  }
  if (address < lower || address >= USER_HEAP_END ||
      length > USER_HEAP_END - address)
    return VM_ERROR_INVALID;

  if (operation == VM_MAP) {
    if (!user_vm_range_free(address, length))
      return VM_ERROR_EXISTS;
    size_t mapped = 0;
    for (; mapped < length; mapped += VM_PAGE_SIZE) {
      if (!page_link(address + mapped)) {
        if (mapped)
          arch_user_unmap(address, mapped);
        return VM_ERROR_NOMEM;
      }
    }
    return address;
  }
  bool success = operation == VM_PROTECT
                     ? arch_user_protect(address, length, protection)
                     : arch_user_unmap(address, length);
  return success ? 0 : VM_ERROR_INVALID;
}
