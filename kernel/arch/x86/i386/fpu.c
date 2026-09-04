#include <arch/x86/i386/control.h>
#include <arch/x86/i386/fpu.h>
#include <dos.h>
#include <smp.h>

static mtask *fpu_owners[SMP_MAX_CPUS];

static void fpu_trap_enable(void) {
  uint32_t cr0 = x86_cr0_read();
  x86_cr0_write((cr0 | X86_CR0_MP | X86_CR0_NE | X86_CR0_TS) & ~X86_CR0_EM);
}

void arch_fpu_init_cpu(void) {
  fpu_owners[smp_current_cpu()] = NULL;
  fpu_trap_enable();
}

void arch_fpu_flush_cpu(void) {
  uint32_t cpu = smp_current_cpu();
  mtask *owner = fpu_owners[cpu];
  if (owner != NULL) {
    uint32_t cr0 = x86_cr0_read();
    x86_cr0_write((cr0 | X86_CR0_MP | X86_CR0_NE) &
                  ~(X86_CR0_EM | X86_CR0_TS));
    asm volatile("fnsave %0" : "=m"(owner->fpu_state) : : "memory");
    owner->fpu_initialized = true;
    fpu_owners[cpu] = NULL;
  }
  fpu_trap_enable();
}

void arch_fpu_reset(mtask *task) {
  uint32_t cpu = smp_current_cpu();
  if (fpu_owners[cpu] == task) {
    fpu_owners[cpu] = NULL;
    fpu_trap_enable();
  }
  task->fpu_initialized = false;
}

void arch_fpu_handle_device_not_available(mtask *task) {
  uint32_t cpu = smp_current_cpu();
  asm volatile("clts" : : : "memory");

  mtask *owner = fpu_owners[cpu];
  if (owner == task) {
    return;
  }
  if (owner != NULL) {
    asm volatile("fnsave %0" : "=m"(owner->fpu_state) : : "memory");
    owner->fpu_initialized = true;
  }
  if (task->fpu_initialized) {
    asm volatile("frstor %0" : : "m"(task->fpu_state) : "memory");
  } else {
    asm volatile("fninit" : : : "memory");
    task->fpu_initialized = true;
  }
  fpu_owners[cpu] = task;
}
