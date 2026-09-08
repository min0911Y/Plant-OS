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
  if (!protection) {
    errno = ENOTSUP;
    return -1;
  }
  if ((protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
    errno = EACCES;
    return -1;
  }
  return vm_protect(address, size, VM_READ | protection);
}

void *mmap(void *address, size_t length, int protection, int flags,
           int descriptor, off_t offset) {
  size_t size = mapping_size(length);
  if (!size || (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC))) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if (!protection ||
      (flags & ~(MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE)) ||
      (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) !=
          (MAP_PRIVATE | MAP_ANONYMOUS) ||
      descriptor != -1 || offset) {
    errno = ENOTSUP;
    return MAP_FAILED;
  }
  if ((protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
    errno = EACCES;
    return MAP_FAILED;
  }
  if ((flags & MAP_FIXED_NOREPLACE) &&
      (!address || ((uintptr_t)address & (VM_PAGE_SIZE - 1)))) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  address = (void *)((uintptr_t)address & ~(uintptr_t)(VM_PAGE_SIZE - 1));
  void *mapping = vm_map(address, size);
  if (!mapping && address && !(flags & MAP_FIXED_NOREPLACE))
    mapping = vm_map(NULL, size);
  if (!mapping)
    return MAP_FAILED;
  if (protection != (PROT_READ | PROT_WRITE) &&
      mprotect(mapping, size, protection)) {
    int error = errno;
    vm_unmap(mapping, size);
    errno = error;
    return MAP_FAILED;
  }
  return mapping;
}

int munmap(void *address, size_t length) {
  size_t size = mapping_size(length);
  if (!size || ((uintptr_t)address & (VM_PAGE_SIZE - 1))) {
    errno = EINVAL;
    return -1;
  }
  return vm_unmap(address, size);
}
