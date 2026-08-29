#ifndef KERNEL_ARCH_X86_FPU_H
#define KERNEL_ARCH_X86_FPU_H

#include <ctypes.h>

typedef struct {
  uint16_t control;
  uint16_t reserved_control;
  uint16_t status;
  uint16_t reserved_status;
  uint16_t tag;
  uint16_t reserved_tag;
  uint32_t instruction_pointer;
  uint32_t instruction_selector_opcode;
  uint32_t data_pointer;
  uint32_t data_selector;
  uint8_t registers[80];
} __attribute__((packed)) x86_fpu_state_t;

#ifdef __cplusplus
static_assert(sizeof(x86_fpu_state_t) == 108, "x86 FPU state size");
#else
_Static_assert(sizeof(x86_fpu_state_t) == 108, "x86 FPU state size");
#endif

struct mtask;

void x86_fpu_init_cpu(void);
void x86_fpu_flush_cpu(void);
void x86_fpu_reset(struct mtask *task);
void x86_fpu_handle_device_not_available(struct mtask *task);

#endif
