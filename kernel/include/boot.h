#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <ctypes.h>
#include <stddef.h>

enum { BOOT_INITRAMFS_DRIVE = 'R' };
enum { BOOT_MEMORY_RANGE_CAPACITY = 64 };

typedef enum {
  BOOT_MEMORY_RESERVED,
  BOOT_MEMORY_USABLE,
} boot_memory_type_t;

typedef struct {
  uint64_t base;
  uint64_t length;
  boot_memory_type_t type;
} boot_memory_range_t;

typedef struct {
  uintptr_t address;
  uint32_t size;
} boot_module_t;

typedef struct {
  boot_module_t initramfs;
  boot_memory_range_t memory_ranges[BOOT_MEMORY_RANGE_CAPACITY];
  uint32_t memory_range_count;
} boot_info_t;

bool boot_initramfs_register(const boot_module_t *module);

#endif
