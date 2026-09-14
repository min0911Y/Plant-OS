/*
 * Plant OS closure allocation for LWJGL's libffi integration.
 *
 * A closure contains writable libffi state and an executable trampoline.  The
 * two views below refer to the same private pages, so ffi_prep_closure_loc can
 * initialize the state through the RW view while native code enters through
 * the RX view.  This keeps the allocator within Plant OS W^X rules.
 */
#include <stddef.h>
#include <stdint.h>

#include <vm.h>

#include "ffi.h"

enum { PLANT_CLOSURE_OFFSET = 32 };

typedef struct {
  void *source;
  void *code;
  size_t length;
} plant_closure_header_t;

static size_t page_length(size_t size) {
  if (size > SIZE_MAX - (VM_PAGE_SIZE - 1))
    return 0;
  return (size + VM_PAGE_SIZE - 1) & ~(size_t)(VM_PAGE_SIZE - 1);
}

void *ffi_closure_alloc(size_t size, void **code) {
  if (!code || size > SIZE_MAX - PLANT_CLOSURE_OFFSET)
    return NULL;

  size_t length = page_length(size + PLANT_CLOSURE_OFFSET);
  if (!length)
    return NULL;

  void *source = vm_map(NULL, length);
  if (!source)
    return NULL;
  void *executable = vm_map_alias(source, NULL, length);
  if (!executable || vm_protect(executable, length, VM_READ | VM_EXEC)) {
    if (executable)
      vm_unmap(executable, length);
    vm_unmap(source, length);
    return NULL;
  }

  plant_closure_header_t *header = source;
  *header = (plant_closure_header_t){source, executable, length};
  *code = (char *)executable + PLANT_CLOSURE_OFFSET;
  return (char *)source + PLANT_CLOSURE_OFFSET;
}

void ffi_closure_free(void *pointer) {
  if (!pointer)
    return;
  plant_closure_header_t *header =
      (plant_closure_header_t *)((char *)pointer - PLANT_CLOSURE_OFFSET);
  vm_unmap(header->code, header->length);
  vm_unmap(header->source, header->length);
}

void *ffi_data_to_code_pointer(void *data) { return data; }

int ffi_tramp_is_present(void *pointer) {
  (void)pointer;
  return 0;
}
