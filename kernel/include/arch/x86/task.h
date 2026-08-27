#ifndef KERNEL_ARCH_X86_TASK_H
#define KERNEL_ARCH_X86_TASK_H

#include <ctypes.h>

typedef struct {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t esi;
  uint32_t edi;
  uint32_t ebp;
  uint32_t eip;
} arch_task_context_t;

#ifdef __cplusplus
static_assert(__builtin_offsetof(arch_task_context_t, eax) == 0,
              "x86 task context eax offset");
static_assert(__builtin_offsetof(arch_task_context_t, ebp) == 24,
              "x86 task context ebp offset");
static_assert(__builtin_offsetof(arch_task_context_t, eip) == 28,
              "x86 task context eip offset");
static_assert(sizeof(arch_task_context_t) == 32, "x86 task context size");
#else
_Static_assert(__builtin_offsetof(arch_task_context_t, eax) == 0,
               "x86 task context eax offset");
_Static_assert(__builtin_offsetof(arch_task_context_t, ebp) == 24,
               "x86 task context ebp offset");
_Static_assert(__builtin_offsetof(arch_task_context_t, eip) == 28,
               "x86 task context eip offset");
_Static_assert(sizeof(arch_task_context_t) == 32, "x86 task context size");
#endif

#endif
