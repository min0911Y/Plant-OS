#include <dos.h>
#include <user_vm.h>

/* Only file-backed extents need metadata. Anonymous allocation remains entirely
 * represented by leaf PTEs. Extents refer to immutable cache-page backing. */
typedef struct vm_file_region {
  struct vm_file_region *next;
  arch_address_space_t root;
  uintptr_t address;
  size_t length, first;
  vfs_mapping_t *backing;
} vm_file_region_t;

static vm_file_region_t *file_regions;
static lock_t mapping_lock;

/* Every caller takes this once around a complete VM operation and releases it
 * at the same level; nested helpers assume the lock is already held. */
void user_vm_lock(void) { lock(&mapping_lock); }
void user_vm_unlock(void) { unlock(&mapping_lock); }

static vm_file_region_t *file_region_at(uintptr_t address) {
  arch_address_space_t root = current_task()->address_space;
  for (vm_file_region_t *region = file_regions; region; region = region->next)
    if (region->root == root && address >= region->address &&
        address - region->address < region->length)
      return region;
  return NULL;
}

bool user_vm_file_page(uintptr_t address) {
  return file_region_at(address) != NULL;
}

bool user_vm_clone(arch_address_space_t source, arch_address_space_t target) {
  vm_file_region_t *copies = NULL;
  for (vm_file_region_t *region = file_regions; region; region = region->next) {
    if (region->root != source)
      continue;
    vm_file_region_t *copy = malloc(sizeof(*copy));
    if (!copy) {
      while (copies) {
        copy = copies;
        copies = copy->next;
        vfs_mapping_release(copy->backing);
        free(copy);
      }
      return false;
    }
    *copy = *region;
    copy->root = target;
    vfs_mapping_retain(copy->backing);
    copy->next = copies;
    copies = copy;
  }
  while (copies) {
    vm_file_region_t *copy = copies;
    copies = copy->next;
    copy->next = file_regions;
    file_regions = copy;
  }
  return true;
}

/* Called with the lock held; the address space is already unpublished, so no
 * sibling thread can create a new extent while it is being torn down. */
void user_vm_release_locked(arch_address_space_t root) {
  vm_file_region_t **link = &file_regions;
  while (*link) {
    vm_file_region_t *region = *link;
    if (region->root != root) {
      link = &region->next;
      continue;
    }
    *link = region->next;
    int status = vfs_mapping_sync(region->backing, region->first,
                                  region->length / VM_PAGE_SIZE);
    if (status)
      logk("vm: file mapping writeback failed during exit: %d\n", status);
    vfs_mapping_release(region->backing);
    free(region);
  }
}

void user_vm_release(arch_address_space_t root) {
  user_vm_lock();
  user_vm_release_locked(root);
  user_vm_unlock();
}

/* Every operation in a range is uniform between file extents, so the range is
 * walked as [anonymous gap][extent][gap]...[end] instead of probing the list
 * once per page. */
static vm_file_region_t *file_region_next(arch_address_space_t root,
                                          uintptr_t from, uintptr_t end) {
  vm_file_region_t *first = NULL;
  for (vm_file_region_t *region = file_regions; region; region = region->next) {
    if (region->root != root || region->address >= end ||
        region->address + region->length <= from)
      continue;
    if (!first || region->address < first->address)
      first = region;
  }
  return first;
}

typedef struct {
  arch_address_space_t root;
  uintptr_t cursor, end;
  vm_file_region_t *region;
} file_range_t;

static size_t file_range_next(file_range_t *range) {
  if (range->cursor >= range->end)
    return 0;
  range->region = file_region_next(range->root, range->cursor, range->end);
  uintptr_t stop = range->end;
  if (range->region) {
    stop = range->cursor < range->region->address
               ? range->region->address
               : range->region->address + range->region->length;
    if (stop > range->end)
      stop = range->end;
    if (range->cursor < range->region->address)
      range->region = NULL;
  }
  size_t span = stop - range->cursor;
  range->cursor = stop;
  return span;
}

static int file_mapping_sync(vm_file_region_t *region, uintptr_t address,
                             size_t length) {
  return vfs_mapping_sync(region->backing,
                          region->first +
                              (address - region->address) / VM_PAGE_SIZE,
                          length / VM_PAGE_SIZE);
}

/* Protect and sync either keep the backing or flush it out, so they can run
 * over every extent before the page table changes at all. */
static intptr_t user_file_examine(unsigned operation, uintptr_t address,
                                  size_t length, unsigned protection) {
  file_range_t range = {current_task()->address_space, address,
                        address + length, NULL};
  if (operation == VM_PROTECT || operation == VM_SYNC) {
    for (size_t span; (span = file_range_next(&range));) {
      uintptr_t cursor = range.cursor - span;
      if (!arch_user_validate(cursor, span)) {
        return VM_ERROR_INVALID;
      }
      if (operation == VM_PROTECT && range.region && (protection & VM_WRITE) &&
          !range.region->backing->writable)
        return VFS_ERROR_ACCESS;
    }
  }
  range.cursor = address;
  for (size_t span; (span = file_range_next(&range));) {
    uintptr_t cursor = range.cursor - span;
    if (operation == VM_PROTECT) {
      if (!arch_user_protect(cursor, span, protection))
        return VM_ERROR_INVALID;
    } else if (range.region) {
      int status = file_mapping_sync(range.region, cursor, span);
      if (status)
        return status;
    }
  }
  return 0;
}

/* A private page can be dropped by remapping it from immutable file backing;
 * shared extents are written back so no dirty alias survives the discard. */
static bool user_file_discard(uintptr_t address, size_t length) {
  file_range_t range = {current_task()->address_space, address,
                        address + length, NULL};
  for (size_t span; (span = file_range_next(&range));) {
    uintptr_t cursor = range.cursor - span;
    vm_file_region_t *region = range.region;
    if (!region) {
      if (!arch_user_discard(cursor, span))
        return false;
    } else if (!region->backing->shared) {
      while (cursor < range.cursor) {
        unsigned flags = arch_user_page_flags(cursor);
        size_t run = VM_PAGE_SIZE;
        while (run < range.cursor - cursor &&
               arch_user_page_flags(cursor + run) == flags)
          run += VM_PAGE_SIZE;
        size_t first = region->first + (cursor - region->address) / VM_PAGE_SIZE;
        if (!arch_user_map_pages(cursor, run,
                                 flags & (VM_READ | VM_WRITE | VM_EXEC), true,
                                 &region->backing->pages[first], false))
          return false;
        cursor += run;
      }
    } else if (file_mapping_sync(region, cursor, span)) {
      return false;
    }
  }
  return true;
}

/* An interval can split at most one non-overlapping extent into two. Allocate
 * that node and flush shared contents before any page-table mutation. */
static int file_regions_prepare(uintptr_t address, size_t length,
                                vm_file_region_t **split) {
  uintptr_t end = address + length;
  arch_address_space_t root = current_task()->address_space;
  for (vm_file_region_t *region = file_regions; region; region = region->next) {
    uintptr_t region_end = region->address + region->length;
    if (region->root != root || end <= region->address || address >= region_end)
      continue;
    if (address > region->address && end < region_end) {
      *split = malloc(sizeof(**split));
      if (!*split)
        return VM_ERROR_NOMEM;
    }
    int status = file_mapping_sync(
        region, address > region->address ? address : region->address,
        (end < region_end ? end : region_end) -
            (address > region->address ? address : region->address));
    if (status)
      return status;
  }
  return 0;
}

static void file_regions_remove(uintptr_t address, size_t length,
                                vm_file_region_t *split) {
  uintptr_t end = address + length;
  arch_address_space_t root = current_task()->address_space;
  vm_file_region_t **link = &file_regions;
  while (*link) {
    vm_file_region_t *region = *link;
    uintptr_t region_end = region->address + region->length;
    if (region->root != root || end <= region->address ||
        address >= region_end) {
      link = &region->next;
      continue;
    }
    if (address <= region->address && end >= region_end) {
      *link = region->next;
      vfs_mapping_release(region->backing);
      free(region);
      continue;
    }
    if (address > region->address) {
      if (end < region_end) {
        *split = *region;
        split->address = end;
        split->length = region_end - end;
        split->first += (end - region->address) / VM_PAGE_SIZE;
        vfs_mapping_retain(split->backing);
        region->next = split;
        split = NULL;
      }
      region->length = address - region->address;
    } else {
      region->first += (end - region->address) / VM_PAGE_SIZE;
      region->address = end;
      region->length = region_end - end;
    }
    link = &region->next;
  }
  free(split);
}

static int user_file_map(uintptr_t address, size_t length,
                         const vm_request_t *request) {
  vm_file_region_t *added = malloc(sizeof(*added));
  if (!added)
    return VM_ERROR_NOMEM;
  memset(added, 0, sizeof(*added));
  int status = vfs_mapping_create(current_task()->fs_context,
                                  request->descriptor, request->offset, length,
                                  request->flags & VM_SHARED, &added->backing);
  if (status)
    goto done;
  if ((request->protection & VM_WRITE) && !added->backing->writable) {
    status = VFS_ERROR_ACCESS;
    goto done;
  }
  added->root = current_task()->address_space;
  added->address = address;
  added->length = length;
  mtask *task = current_task();
  vm_file_region_t *split = NULL;
  if (address < task->alloc_addr + *task->alloc_size)
    status = VM_ERROR_INVALID;
  else
    status = file_regions_prepare(address, length, &split);
  if (!status) {
    if (arch_user_map_pages(address, length, request->protection,
                            request->flags & VM_REPLACE, added->backing->pages,
                            added->backing->shared)) {
      file_regions_remove(address, length, split);
      added->next = file_regions;
      file_regions = added;
      return 0;
    }
    status = VM_ERROR_NOMEM;
  }
  free(split);
done:
  if (added->backing)
    vfs_mapping_release(added->backing);
  free(added);
  return status;
}

intptr_t user_vm_apply(unsigned operation, uintptr_t address,
                       const vm_request_t *request) {
  size_t length = request->length;
  if (operation == VM_DISCARD) {
    /* Discard keeps the current protection, so every page must still be
     * readable before any of them is dropped. */
    for (size_t offset = 0; offset < length; offset += VM_PAGE_SIZE)
      if (!(arch_user_page_flags(address + offset) & VM_READ))
        return VM_ERROR_INVALID;
    return user_file_discard(address, length) ? 0 : VM_ERROR_NOMEM;
  }
  if (operation == VM_PROTECT || operation == VM_SYNC)
    return user_file_examine(operation, address, length, request->protection);
  if (operation == VM_MAP && (request->flags & VM_FILE))
    return user_file_map(address, length, request);
  /* A replaced file range keeps its old bytes until the new mapping exists. */
  vm_file_region_t *split = NULL;
  int status = file_regions_prepare(address, length, &split);
  if (status)
    goto done;
  mtask *task = current_task();
  if (address < task->alloc_addr + *task->alloc_size) {
    status = VM_ERROR_INVALID;
    goto done;
  }
  bool success;
  if (operation == VM_UNMAP)
    success = arch_user_unmap(address, length);
  else
    success = arch_user_map(address, length, request->protection,
                            request->flags & VM_REPLACE);
  if (!success) {
    status = operation == VM_MAP ? VM_ERROR_NOMEM : VM_ERROR_INVALID;
    goto done;
  }
  file_regions_remove(address, length, split);
  split = NULL;
done:
  free(split);
  return status;
}
