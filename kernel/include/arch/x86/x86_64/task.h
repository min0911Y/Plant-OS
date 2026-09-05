#ifndef KERNEL_X86_64_TASK_H
#define KERNEL_X86_64_TASK_H

#include <ctypes.h>

typedef uintptr_t arch_address_space_t;

typedef struct {
  uint16_t control, status;
  uint8_t tag, reserved0;
  uint16_t opcode;
  uint64_t instruction_pointer, data_pointer;
  uint32_t mxcsr, mxcsr_mask;
  uint8_t x87[8][16];
  uint8_t xmm[16][16];
  uint8_t reserved1[96];
} __attribute__((aligned(16))) arch_fpu_state_t;

/* SysV callee-saved registers, followed by RET's target and its return slot. */
typedef struct {
  uint64_t r15, r14, r13, r12, rbx, rbp, rip, return_address;
} arch_task_context_t;

/* Hardware and SYSCALL entries use the same frame, including SSE state. */
typedef struct {
  arch_fpu_state_t simd;
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
  uint64_t vector, error;
  uint64_t rip, cs, rflags, rsp, ss;
} x64_interrupt_frame_t;

#ifdef __cplusplus
#define X64_STATIC_ASSERT static_assert
#else
#define X64_STATIC_ASSERT _Static_assert
#endif
X64_STATIC_ASSERT(sizeof(x64_interrupt_frame_t) == 688, "x86_64 entry frame");
X64_STATIC_ASSERT(__builtin_offsetof(x64_interrupt_frame_t, rip) == 648,
                  "x86_64 hardware frame offset");

#undef X64_STATIC_ASSERT

struct mtask;
void x64_simd_switch(struct mtask *next);
void x64_return_to_user(x64_interrupt_frame_t *frame) __attribute__((noreturn));

#endif
