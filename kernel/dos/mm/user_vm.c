#include <dos.h>
#include <irq.h>
#include <page_fault.h>
#include <user_space.h>
#include <user_vm.h>

bool user_vm_range_free(uintptr_t address, size_t length) {
  return arch_user_find_free(address, address + length, length, VM_PAGE_SIZE) ==
         address;
}

bool user_vm_readable(uintptr_t address, size_t length) {
  if (address < USER_SPACE_START || address >= USER_HEAP_END || !length ||
      length > USER_HEAP_END - address)
    return false;
  uintptr_t last = (address + length - 1) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
  for (uintptr_t page = address & ~(uintptr_t)(VM_PAGE_SIZE - 1);;
       page += VM_PAGE_SIZE) {
    if (!(arch_user_page_flags(page) & VM_READ))
      return false;
    if (page == last)
      return true;
  }
}

bool user_vm_copy_from(void *destination, uintptr_t address, size_t length) {
  irq_state_t state = irq_save();
  bool readable = user_vm_readable(address, length);
  if (readable)
    memcpy(destination, (const void *)address, length);
  irq_restore(state);
  return readable;
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

bool user_vm_copy_to(uintptr_t address, const void *source, size_t length) {
  irq_state_t state = irq_save();
  bool writable = user_vm_prepare_write(address, length);
  if (writable)
    memcpy((void *)address, source, length);
  irq_restore(state);
  return writable;
}

/* Reject a request before it touches the address space. Mapping permits every
 * documented flag, the remaining operations are exact and flag free. */
static bool vm_request_supported(unsigned operation,
                                 const vm_request_t *request) {
  if (operation >= VM_OPERATION_COUNT || !request->length ||
      (request->reserved | request->flags) >> 3 ||
      (request->protection & ~(VM_READ | VM_WRITE | VM_EXEC)) ||
      ((request->protection & (VM_WRITE | VM_EXEC)) == (VM_WRITE | VM_EXEC)))
    return false;
  if (operation != VM_MAP)
    return !(request->flags | request->alignment | request->offset |
             request->descriptor) &&
           (operation == VM_PROTECT || !request->protection);
  if (request->flags & VM_FILE)
    return !(request->offset & (VM_PAGE_SIZE - 1));
  return !(request->offset || request->descriptor ||
           (request->flags & VM_SHARED));
}

intptr_t user_vm_operation(unsigned operation, const vm_request_t *request) {
  mtask *task = current_task();
  mtask *owner = get_task(task->tgid);
  if (!owner)
    owner = task;
  uintptr_t address = request->address;
  size_t length = request->length;
  size_t alignment = request->alignment;
  if (!task->alloc_size || !vm_request_supported(operation, request) ||
      ((address | length) & (VM_PAGE_SIZE - 1)) ||
      (operation == VM_MAP &&
       (alignment < VM_PAGE_SIZE || (alignment & (alignment - 1)) ||
        (address & (alignment - 1)) ||
        ((request->flags & (VM_REPLACE | VM_FILE)) == VM_REPLACE && !address))))
    return VM_ERROR_INVALID;

  uintptr_t lower = task->alloc_addr + *task->alloc_size;
  if (lower < task->alloc_addr || lower >= USER_HEAP_END ||
      length > USER_HEAP_END - lower)
    return VM_ERROR_NOMEM;
  if (!address && operation == VM_MAP) {
    /* Search downward above the heap. Page tables are the allocation registry;
     * fork and task teardown therefore need no second list of mappings. */
    uintptr_t upper = owner->vm_hint > lower ? owner->vm_hint : USER_HEAP_END;
    address = length <= upper - lower
                  ? arch_user_find_free(lower, upper, length, alignment)
                  : 0;
    if (!address && upper != USER_HEAP_END)
      address = arch_user_find_free(lower, USER_HEAP_END, length, alignment);
    if (!address)
      return VM_ERROR_NOMEM;
  }
  if (address < lower || address >= USER_HEAP_END ||
      length > USER_HEAP_END - address)
    return VM_ERROR_INVALID;

  if (operation == VM_MAP) {
    if (!(request->flags & VM_REPLACE) && !user_vm_range_free(address, length))
      return VM_ERROR_EXISTS;
    intptr_t result = user_vm_apply(operation, address, request);
    if (result < 0)
      return result;
    owner->vm_hint = address;
    return address;
  }
  intptr_t result = user_vm_apply(operation, address, request);
  if (!result && operation == VM_UNMAP && address + length > owner->vm_hint)
    owner->vm_hint = address + length;
  return result;
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
