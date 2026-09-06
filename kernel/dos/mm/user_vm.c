#include <dos.h>
#include <page_fault.h>
#include <user_space.h>
#include <user_vm.h>

bool user_vm_range_free(uintptr_t address, size_t length) {
  return arch_user_find_free(address, address + length, length) == address;
}

bool user_vm_prepare_write(uintptr_t address, size_t length) {
  if (address < USER_SPACE_START || address >= USER_HEAP_END || !length ||
      length > USER_HEAP_END - address)
    return false;
  uintptr_t last = (address + length - 1) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
  for (uintptr_t page = address & ~(uintptr_t)(VM_PAGE_SIZE - 1);;
       page += VM_PAGE_SIZE) {
    if (!(arch_user_page_flags(page) & VM_WRITE) ||
        arch_page_fault_resolve(page, 3) == PAGE_FAULT_NO_MEMORY)
      return false;
    if (page == last)
      return true;
  }
}

intptr_t user_vm_operation(unsigned operation, const vm_request_t *request) {
  mtask *task = current_task();
  mtask *owner = get_task(task->tgid);
  if (!owner)
    owner = task;
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
    uintptr_t upper = owner->vm_hint > lower ? owner->vm_hint : USER_HEAP_END;
    address =
        length <= upper - lower ? arch_user_find_free(lower, upper, length) : 0;
    if (!address && upper != USER_HEAP_END)
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
    owner->vm_hint = address;
    return address;
  }
  bool success = operation == VM_PROTECT
                     ? arch_user_protect(address, length, protection)
                     : arch_user_unmap(address, length);
  if (success && operation == VM_UNMAP && address + length > owner->vm_hint)
    owner->vm_hint = address + length;
  return success ? 0 : VM_ERROR_INVALID;
}

bool page_fault_try_resolve(uintptr_t address, uint32_t error) {
  page_fault_result_t result = arch_page_fault_resolve(address, error);
  if (result == PAGE_FAULT_NO_MEMORY) {
    logk("memory: task %u exhausted memory during copy-on-write\n",
         current_task()->tid);
    // A syscall may fault while filling a user's lazy stack or heap page.
    // Abandon that syscall before terminating, leaving the scheduler's lock.
    while (kernel_lock_depth() > 1)
      kernel_lock_leave();
    task_exit((unsigned)-1);
  }
  return result == PAGE_FAULT_RESOLVED;
}
