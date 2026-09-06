#ifndef KERNEL_PAGE_FAULT_H
#define KERNEL_PAGE_FAULT_H

#include <ctypes.h>
#include <stddef.h>

typedef enum {
  PAGE_FAULT_UNHANDLED,
  PAGE_FAULT_RESOLVED,
  PAGE_FAULT_NO_MEMORY,
} page_fault_result_t;

page_fault_result_t arch_page_fault_resolve(uintptr_t address, uint32_t error);
bool page_fault_try_resolve(uintptr_t address, uint32_t error);

#endif
