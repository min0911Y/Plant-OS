#ifndef KERNEL_USER_SPACE_H
#define KERNEL_USER_SPACE_H

#include <ctypes.h>

#if defined(KERNEL_ARCH_I386)
#include <arch/x86/i386/user.h>
#else
#error "User address-space layout is not defined for the selected architecture"
#endif

struct user_runtime_layout {
  uint32_t total_pages;
  uintptr_t stack_top;
  uintptr_t allocation_base;
};

bool user_runtime_layout_calculate(uintptr_t aligned_image_end,
                                   size_t heap_pages, size_t stack_pages,
                                   bool uses_status_page, uintptr_t entry,
                                   struct user_runtime_layout *layout);

#endif
