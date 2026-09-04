#ifndef KERNEL_EXECUTABLE_H
#define KERNEL_EXECUTABLE_H

#include <ctypes.h>

bool arch_executable_validate(const void *image, size_t image_size,
                              uintptr_t *entry_out,
                              uintptr_t *image_end_out);
bool arch_executable_load(const void *image, size_t image_size,
                          uintptr_t *entry_out);

#endif
