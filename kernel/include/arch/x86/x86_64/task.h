#ifndef KERNEL_X86_64_TASK_H
#define KERNEL_X86_64_TASK_H

#include <ctypes.h>

typedef uintptr_t arch_address_space_t;
enum { ARCH_SHARED_ADDRESS_SPACE_SMP = 1 };

#include <signal_context.h>
typedef signal_fpregs_t x64_fx_state_t;

/* Standard XSAVE layout for the enabled x87, SSE and optional AVX state. */
typedef struct {
  x64_fx_state_t legacy;
  uint64_t xstate_bv, xcomp_bv;
  uint64_t reserved[6];
  uint8_t ymm_hi[16][16];
} __attribute__((aligned(64))) arch_fpu_state_t;

/* SysV callee-saved registers, followed by RET's target and its return slot. */
typedef struct {
  uint64_t r15, r14, r13, r12, rbx, rbp, rip, return_address;
} arch_task_context_t;

/* Entry/signal frames contain register data only, never an XSAVE header. */
typedef mcontext_t x64_interrupt_frame_t;

#ifdef __cplusplus
#define X64_STATIC_ASSERT static_assert
#else
#define X64_STATIC_ASSERT _Static_assert
#endif
X64_STATIC_ASSERT(sizeof(x64_fx_state_t) == 512, "x86_64 FXSAVE area");
X64_STATIC_ASSERT(__builtin_offsetof(x64_fx_state_t, ymm_inuse) == 464,
                  "x86_64 entry YMM flag offset");
X64_STATIC_ASSERT(sizeof(arch_fpu_state_t) == 832, "x86_64 XSAVE area");
X64_STATIC_ASSERT(__alignof__(arch_fpu_state_t) == 64, "x86_64 XSAVE alignment");
X64_STATIC_ASSERT(__builtin_offsetof(arch_fpu_state_t, xstate_bv) == 512,
                  "x86_64 XSAVE header offset");
X64_STATIC_ASSERT(__builtin_offsetof(arch_fpu_state_t, ymm_hi) == 576,
                  "x86_64 XSAVE YMM offset");
X64_STATIC_ASSERT(__builtin_offsetof(x64_interrupt_frame_t, ymm_hi) == 512,
                  "x86_64 entry YMM offset");
X64_STATIC_ASSERT(__builtin_offsetof(x64_interrupt_frame_t, vector) == 888,
                  "x86_64 entry vector offset");
X64_STATIC_ASSERT(__builtin_offsetof(x64_interrupt_frame_t, cs) == 912,
                  "x86_64 entry CS offset");
X64_STATIC_ASSERT(sizeof(x64_interrupt_frame_t) == 944, "x86_64 entry frame");
X64_STATIC_ASSERT(__builtin_offsetof(x64_interrupt_frame_t, rip) == 904,
                  "x86_64 hardware frame offset");

#undef X64_STATIC_ASSERT

struct mtask;
void x64_simd_switch(struct mtask *next);
void x64_return_to_user(x64_interrupt_frame_t *frame) __attribute__((noreturn));

#endif
