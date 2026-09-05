#include <arch/x86/x86_64/cpu.h>
#include <dos.h>
#include <string.h>

static arch_fpu_state_t initial_state;
uint32_t x64_mxcsr_mask;

void arch_fpu_init_cpu(void) {
  uintptr_t control;
  __asm__ volatile("mov %%cr0, %0" : "=r"(control));
  control = (control & ~12ull) | 0x22;
  __asm__ volatile("mov %0, %%cr0" : : "r"(control) : "memory");
  __asm__ volatile("mov %%cr4, %0" : "=r"(control));
  /* This ABI saves SSE state with FXSAVE. User GS stays zero; only SWAPGS
   * changes it, including in the NMI entry's transition window. */
  control = (control & ~((1ull << 16) | (1ull << 18))) | 0x600;
  __asm__ volatile("mov %0, %%cr4" : : "r"(control) : "memory");
  uint32_t mxcsr = 0x1f80;
  __asm__ volatile("fninit; ldmxcsr %0" : : "m"(mxcsr) : "memory");
  if (smp_current_cpu() == 0) {
    __asm__ volatile("fxsave64 %0" : "=m"(initial_state) : : "memory");
    x64_mxcsr_mask =
        initial_state.mxcsr_mask ? initial_state.mxcsr_mask : 0xffbf;
    memset(&initial_state, 0, sizeof(initial_state));
    /* A clean architectural image also clears all 16 XMM registers. */
    initial_state.control = 0x37f;
    initial_state.mxcsr = mxcsr;
    initial_state.mxcsr_mask = x64_mxcsr_mask;
  }
  __asm__ volatile("fxrstor64 %0" : : "m"(initial_state) : "memory");
}

void arch_fpu_flush_cpu(void) {
  mtask *task = current_task();
  __asm__ volatile("fxsave64 %0" : "=m"(task->fpu_state) : : "memory");
  task->fpu_initialized = 1;
}
void x64_simd_switch(mtask *next) {
  const arch_fpu_state_t *state =
      next->fpu_initialized ? &next->fpu_state : &initial_state;
  __asm__ volatile("fxrstor64 %0" : : "m"(*state) : "memory");
}
void arch_fpu_reset(mtask *task) {
  task->fpu_state = initial_state;
  task->fpu_initialized = 1;
}
void arch_fpu_handle_device_not_available(mtask *task) {
  (void)task;
  Panic_K("unexpected #NM in eager SSE backend");
  arch_halt();
}
