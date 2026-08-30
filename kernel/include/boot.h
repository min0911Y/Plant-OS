#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <ctypes.h>
#include <stddef.h>

enum { BOOT_INITRAMFS_DRIVE = 'R' };

typedef struct {
  uintptr_t address;
  uint32_t size;
} boot_module_t;

bool boot_initramfs_register(const boot_module_t *module);

#endif
