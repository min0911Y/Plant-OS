#include <dos.h>
#include <executable.h>
#include <irq.h>
#include <user_space.h>
#include <user_thread.h>
#include <user_vm.h>

typedef struct {
  uintptr_t entry, stack_top, argument;
} user_thread_start_t;

static void user_thread_entry(void) {
  mtask *task = current_task();
  user_thread_start_t *request = (void *)task->line;
  user_thread_start_t start = *request;
  page_free_one(request);
  task->line = NULL;
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);
  kernel_lock_leave();
  arch_task_enter_user(start.entry, start.stack_top, start.argument);
}

static bool region_valid(thread_region_t region) {
  if (!region.size)
    return !region.address;
  if (((region.address | region.size) & (VM_PAGE_SIZE - 1)) ||
      region.address >= USER_HEAP_END ||
      region.size > USER_HEAP_END - region.address)
    return false;
  return user_vm_readable(region.address, region.size);
}

static bool region_contains(thread_region_t region, uintptr_t address,
                            size_t size) {
  return address >= region.address && address - region.address <= region.size &&
         size <= region.size - (address - region.address);
}

static bool tls_valid(const native_thread_request_t *request) {
  return region_valid(request->tls) &&
         (!request->thread_pointer
              ? !request->tls.size
              : !(request->thread_pointer & (sizeof(uintptr_t) - 1)) &&
                    region_contains(request->tls, request->thread_pointer,
                                    sizeof(uintptr_t)) &&
                    user_vm_prepare_write(request->thread_pointer,
                                          sizeof(uintptr_t)));
}

int user_thread_create(native_thread_request_t *request) {
  uintptr_t top = request->stack_top;
  if (request->flags & ~THREAD_JOINABLE || (top & 15) ||
      top < USER_SPACE_START + 32 || top > USER_HEAP_END ||
      !user_vm_readable(request->entry, 1) || !tls_valid(request) ||
      !region_valid(request->stack) ||
      (request->stack.size && !region_contains(request->stack, top - 32, 32)) ||
      (request->stack.size && request->tls.size &&
       request->stack.address < request->tls.address + request->tls.size &&
       request->tls.address < request->stack.address + request->stack.size) ||
      !user_vm_prepare_write(top - 32, 32))
    return -22;

  mtask *self = current_task();
  mtask *thread = create_thread_task((uintptr_t)user_thread_entry, 1);
  if (!thread)
    return -12;
  thread->alloc_addr = self->alloc_addr;
  thread->alloc_size = self->alloc_size;
  thread->TTY = self->TTY;
  thread->tty_session = self->tty_session;
  request->name[sizeof(request->name) - 1] = 0;
  task_set_name(thread, request->name[0] ? request->name : "thread");
  user_thread_start_t *start = page_malloc_one_no_mark();
  if (!start) {
    task_abort_creation(thread);
    return -12;
  }
  change_page_task_id(thread->tid, start, sizeof(*start));
  *start = (user_thread_start_t){request->entry, top, request->argument};
  thread->line = (char *)start;
  if (!task_prepare_input(thread)) {
    task_abort_creation(thread);
    return -12;
  }
  thread->thread_pointer = request->thread_pointer;
  thread->joinable = !!(request->flags & THREAD_JOINABLE);
  thread->user_tls = request->tls;
  thread->user_stack = request->stack;
  if (!task_publish(thread)) {
    thread->user_tls = (thread_region_t){0};
    thread->user_stack = (thread_region_t){0};
    task_abort_creation(thread);
    return -12;
  }
  /* The syscall holds IRQs and the kernel lock until the result is copied. */
  request->tid = thread->tid;
  request->generation = thread->generation;
  return 0;
}

int user_thread_set_pointer(const native_thread_request_t *request) {
  mtask *self = current_task();
  if (!request->thread_pointer || !tls_valid(request))
    return -22;
  if (self->thread_pointer)
    return -16;
  self->thread_pointer = request->thread_pointer;
  self->user_tls = request->tls;
  arch_thread_pointer_set(self->thread_pointer);
  return 0;
}

void user_thread_release(mtask *task) {
  if (!task->address_space || (!task->user_tls.size && !task->user_stack.size))
    return;
  arch_address_space_t previous = arch_address_space_current();
  arch_address_space_activate(task->address_space);
  thread_region_t regions[] = {task->user_tls, task->user_stack};
  for (unsigned i = 0; i < 2; i++) {
    if (!regions[i].size ||
        arch_user_unmap(regions[i].address, regions[i].size))
      continue;
    /* A user may have released part of a transferred mapping. The remaining
     * pages still belong to the exiting thread and must not be abandoned. */
    for (size_t offset = 0; offset < regions[i].size; offset += VM_PAGE_SIZE) {
      uintptr_t address = regions[i].address + offset;
      if (arch_user_page_flags(address))
        arch_user_unmap(address, VM_PAGE_SIZE);
    }
  }
  arch_address_space_activate(previous);
  task->user_tls = (thread_region_t){0};
  task->user_stack = (thread_region_t){0};
  task->thread_pointer = 0;
}
