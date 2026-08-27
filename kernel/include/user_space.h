#ifndef KERNEL_USER_SPACE_H
#define KERNEL_USER_SPACE_H

#include <ctypes.h>

#define USER_SPACE_START 0x70000000u
#define USER_HEAP_END 0xf0000000u

struct user_runtime_layout {
  uint32_t total_pages;
  uint32_t stack_top;
  uint32_t allocation_base;
};

bool user_runtime_layout_calculate(uint32_t aligned_image_end,
                                   uint32_t heap_pages,
                                   uint32_t stack_pages,
                                   bool uses_status_page, uint32_t entry,
                                   struct user_runtime_layout *layout);

#endif
