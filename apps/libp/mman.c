#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <vm.h>

static size_t mapping_size(size_t length) {
  return length && length <= SIZE_MAX - (VM_PAGE_SIZE - 1)
             ? (length + VM_PAGE_SIZE - 1) & ~(size_t)(VM_PAGE_SIZE - 1)
             : 0;
}

int mprotect(void *address, size_t length, int protection) {
  size_t size = mapping_size(length);
  if (!size || ((uintptr_t)address & (VM_PAGE_SIZE - 1)) ||
      (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC))) {
    errno = EINVAL;
    return -1;
  }
  if ((protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
    errno = EACCES;
    return -1;
  }
  return vm_protect(address, size, protection);
}

void *mmap(void *address, size_t length, int protection, int flags,
           int descriptor, off_t offset) {
  size_t size = mapping_size(length);
  if (!size || (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC))) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  bool anonymous = flags & MAP_ANONYMOUS;
  int kind = flags & (MAP_PRIVATE | MAP_SHARED);
  if ((kind != MAP_PRIVATE && kind != MAP_SHARED) || offset < 0 ||
      ((uint64_t)offset & (VM_PAGE_SIZE - 1))) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if ((flags & ~(MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED |
                 MAP_FIXED_NOREPLACE | MAP_NORESERVE)) ||
      (anonymous && (kind == MAP_SHARED || descriptor != -1 || offset))) {
    errno = ENOTSUP;
    return MAP_FAILED;
  }
  if ((protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
    errno = EACCES;
    return MAP_FAILED;
  }
  if (((flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) ==
       (MAP_FIXED | MAP_FIXED_NOREPLACE)) ||
      ((flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) &&
       (!address || ((uintptr_t)address & (VM_PAGE_SIZE - 1))))) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  address = (void *)((uintptr_t)address & ~(uintptr_t)(VM_PAGE_SIZE - 1));
  unsigned options = (flags & MAP_FIXED ? VM_REPLACE : 0) |
                     (kind == MAP_SHARED ? VM_SHARED : 0);
  bool fixed = flags & (MAP_FIXED | MAP_FIXED_NOREPLACE);
  for (unsigned attempt = 0; attempt < 2; attempt++) {
    void *mapping =
        anonymous
            ? vm_map_aligned(address, size, VM_PAGE_SIZE, protection, options)
            : vm_map_file(address, size, protection, options, descriptor, offset);
    if (mapping || fixed)
      return mapping ? mapping : MAP_FAILED;
    /* A used address is only a hint for a non-fixed mapping. */
    address = NULL;
  }
  return MAP_FAILED;
}

int munmap(void *address, size_t length) {
  size_t size = mapping_size(length);
  if (!size || ((uintptr_t)address & (VM_PAGE_SIZE - 1))) {
    errno = EINVAL;
    return -1;
  }
  return vm_unmap(address, size);
}

int madvise(void *address, size_t length, int advice) {
  size_t size = mapping_size(length);
  if (!size || ((uintptr_t)address & (VM_PAGE_SIZE - 1)) ||
      (advice != MADV_DONTNEED && advice != MADV_WILLNEED)) {
    errno = EINVAL;
    return -1;
  }
  if (advice == MADV_WILLNEED) {
    return 0;
  }
  return vm_discard(address, size);
}

int mincore(void *address, size_t length, unsigned char *vec) {
  if (address == NULL || length == 0 || vec == NULL) {
    errno = EINVAL;
    return -1;
  }
  uintptr_t offset = (uintptr_t)address & (VM_PAGE_SIZE - 1);
  if (length > SIZE_MAX - offset - (VM_PAGE_SIZE - 1)) {
    errno = EOVERFLOW;
    return -1;
  }
  size_t pages = (length + offset + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE;
  for (size_t index = 0; index < pages; index++) {
    vec[index] = 1;
  }
  return 0;
}

int msync(void *address, size_t length, int flags) {
  size_t size = mapping_size(length);
  if (!size || ((uintptr_t)address & (VM_PAGE_SIZE - 1)) ||
      (flags & ~(MS_SYNC | MS_ASYNC | MS_INVALIDATE)) ||
      (flags & (MS_SYNC | MS_ASYNC)) == (MS_SYNC | MS_ASYNC)) {
    errno = EINVAL;
    return -1;
  }
  return vm_sync(address, size);
}
