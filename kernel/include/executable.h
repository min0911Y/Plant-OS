#ifndef KERNEL_EXECUTABLE_H
#define KERNEL_EXECUTABLE_H

#include <ctypes.h>

typedef enum { EXECUTE_PROGRAM, EXECUTE_COMMAND } execute_mode_t;
int os_execute(char *filename, char *line, execute_mode_t mode);

bool arch_executable_validate(const void *image, size_t image_size,
                              uintptr_t *entry_out, uintptr_t *image_end_out);
bool arch_executable_load(const void *image, size_t image_size,
                          uintptr_t *entry_out);

#endif
