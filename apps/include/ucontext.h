#ifndef PLANT_UCONTEXT_H
#define PLANT_UCONTEXT_H
#include <signal.h>

/* Native register ABI, not a Linux ucontext. Only user register state is
 * restored; segment selectors and privileged flags are validated by the kernel. */
#if __SIZEOF_POINTER__ == 8
typedef struct {
  uint16_t control, status;
  uint8_t tag, reserved0;
  uint16_t opcode;
  uint64_t instruction_pointer, data_pointer;
  uint32_t mxcsr, mxcsr_mask;
  uint8_t x87[8][16];
  uint8_t xmm[16][16];
  uint8_t reserved1[48];
  /* FXSAVE bytes 464..511 belong to software; entry frames use this flag. */
  uint64_t ymm_inuse;
  uint8_t reserved2[40];
} __attribute__((aligned(16))) signal_fpregs_t;

typedef struct {
  signal_fpregs_t simd;
  uint8_t ymm_hi[16][16];
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
  uint64_t vector, error;
  uint64_t rip, cs, rflags, rsp, ss;
} mcontext_t;

#else
typedef struct {
  uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
  uint32_t gs, fs, es, ds, eip, cs, eflags, esp, ss;
  uint8_t fpregs[108];
} mcontext_t;
#endif
typedef struct ucontext {
  struct ucontext *uc_link;
  stack_t uc_stack;
  sigset_t uc_sigmask;
  mcontext_t uc_mcontext;
} ucontext_t;
#endif
