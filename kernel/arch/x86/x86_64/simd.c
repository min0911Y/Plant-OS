#include <arch/x86/cpuid.h>
#include <arch/x86/x86_64/cpu.h>
#include <dos.h>
#include <irq.h>
#include <string.h>

enum { XSTATE_X87_SSE = 3, XSTATE_AVX = 4 };
uint32_t x64_xstate_mask;
uint32_t x64_xgetbv1;
static arch_fpu_state_t initial_state;
static bool use_xsaveopt;
uint32_t x64_mxcsr_mask;

static void simd_restore(const arch_fpu_state_t *state) {
  if (x64_xstate_mask)
    __asm__ volatile("xrstor64 %0" : : "m"(*state), "a"(x64_xstate_mask), "d"(0)
                     : "memory");
  else
    __asm__ volatile("fxrstor64 %0" : : "m"(state->legacy) : "memory");
}

void arch_fpu_init_cpu(void) {
  bool xsave = x86_cpuid(0, 0).eax >= 0xd &&
               (x86_cpuid(1, 0).ecx & (1u << 26)) &&
               (x86_cpuid(0xd, 0).eax & XSTATE_X87_SSE) == XSTATE_X87_SSE;
  uint32_t mask = xsave ? XSTATE_X87_SSE : 0;
  bool optimized = xsave && (x86_cpuid(0xd, 1).eax & 1);
  if (xsave && (x86_cpuid(1, 0).ecx & (1u << 28)) &&
      (x86_cpuid(0xd, 0).eax & XSTATE_AVX)) {
    x86_cpuid_t ymm = x86_cpuid(0xd, 2);
    if (ymm.eax == sizeof(initial_state.ymm_hi) &&
        ymm.ebx == __builtin_offsetof(arch_fpu_state_t, ymm_hi))
      mask |= XSTATE_AVX;
  }
  bool inuse = (mask & XSTATE_AVX) && (x86_cpuid(0xd, 1).eax & 4);
  /* Select one format on the BSP before APs or the scheduler can use it. */
  if (!x64_mxcsr_mask) {
    x64_xstate_mask = mask;
    use_xsaveopt = optimized;
    x64_xgetbv1 = inuse;
  }
  if ((mask & x64_xstate_mask) != x64_xstate_mask ||
      (use_xsaveopt && !optimized) || (x64_xgetbv1 && !inuse)) {
    Panic_K("CPU lacks the selected SIMD context backend");
    arch_halt();
  }
  uintptr_t control;
  __asm__ volatile("mov %%cr0, %0" : "=r"(control));
  control = (control & ~12ull) | 0x22;
  __asm__ volatile("mov %0, %%cr0" : : "r"(control) : "memory");
  __asm__ volatile("mov %%cr4, %0" : "=r"(control));
  /* User GS stays zero; only SWAPGS changes it. */
  control = (control & ~((1ull << 16) | (1ull << 18))) | 0x600;
  if (x64_xstate_mask)
    control |= 1ull << 18;
  __asm__ volatile("mov %0, %%cr4" : : "r"(control) : "memory");
  if (x64_xstate_mask)
    __asm__ volatile("xsetbv" : : "c"(0), "a"(x64_xstate_mask), "d"(0)
                     : "memory");
  uint32_t mxcsr = 0x1f80;
  __asm__ volatile("fninit; ldmxcsr %0" : : "m"(mxcsr) : "memory");
  if (!x64_mxcsr_mask) {
    __asm__ volatile("fxsave64 %0" : "=m"(initial_state.legacy) : : "memory");
    x64_mxcsr_mask =
        initial_state.legacy.mxcsr_mask ? initial_state.legacy.mxcsr_mask : 0xffbf;
    memset(&initial_state, 0, sizeof(initial_state));
    /* An empty XSTATE_BV initializes XMM and YMM as well as x87. */
    initial_state.legacy.control = 0x37f;
    initial_state.legacy.mxcsr = mxcsr;
    initial_state.legacy.mxcsr_mask = x64_mxcsr_mask;
    const char *backend = use_xsaveopt ? "xsaveopt"
                          : x64_xstate_mask ? "xsave" : "fxsave";
    logk("simd: context=%s xcr0=%u xgetbv1=%u\n", backend, x64_xstate_mask,
         x64_xgetbv1);
  }
  simd_restore(&initial_state);
}

void arch_fpu_flush_cpu(void) {
  mtask *task = current_task();
  /* XSAVEOPT may retain unmodified components from the last XRSTOR here.
   * Keep this persistent image intact between restore and save. */
  if (use_xsaveopt)
    __asm__ volatile("xsaveopt64 %0" : "+m"(task->fpu_state)
                     : "a"(x64_xstate_mask), "d"(0) : "memory");
  else if (x64_xstate_mask)
    __asm__ volatile("xsave64 %0" : "+m"(task->fpu_state)
                     : "a"(x64_xstate_mask), "d"(0) : "memory");
  else
    __asm__ volatile("fxsave64 %0" : "=m"(task->fpu_state.legacy) : : "memory");
  task->fpu_initialized = 1;
}
void x64_simd_switch(mtask *next) {
  simd_restore(next->fpu_initialized ? &next->fpu_state : &initial_state);
}
void arch_fpu_reset(mtask *task) {
  irq_state_t flags = irq_save();
  task->fpu_state = initial_state;
  task->fpu_initialized = 1;
  /* Resetting the live image must also reset XRSTOR's modified tracking. */
  if (task == current_task())
    simd_restore(&task->fpu_state);
  irq_restore(flags);
}
void arch_fpu_handle_device_not_available(mtask *task) {
  (void)task;
  Panic_K("unexpected #NM in eager SIMD backend");
  arch_halt();
}
