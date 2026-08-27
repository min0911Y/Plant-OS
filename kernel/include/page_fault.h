#ifndef KERNEL_PAGE_FAULT_H
#define KERNEL_PAGE_FAULT_H

#include <ctypes.h>
#include <stddef.h>

bool page_fault_try_resolve(uintptr_t address, uint32_t error);

#endif
