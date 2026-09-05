#ifndef KERNEL_X86_64_CPU_H
#define KERNEL_X86_64_CPU_H

#include <arch.h>
#include <stdint.h>

typedef struct {
  uint64_t kernel_stack;
  uint64_t user_stack;
  uint32_t index;
  uint32_t lapic_id;
  uint32_t online;
  uint32_t lock_depth;
} x64_cpu_t;

extern uintptr_t x64_hhdm;
extern arch_address_space_t x64_kernel_cr3;
extern uint32_t x64_mxcsr_mask;

static inline uint64_t x64_msr_read(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return (uint64_t)hi << 32 | lo;
}

static inline void x64_msr_write(uint32_t msr, uint64_t value) {
  __asm__ volatile("wrmsr"
                   :
                   : "c"(msr), "a"((uint32_t)value),
                     "d"((uint32_t)(value >> 32))
                   : "memory");
}

static inline x64_cpu_t *x64_this_cpu(void) {
  extern x64_cpu_t x64_cpus[];
  uint32_t index;
  __asm__ volatile("movl %%gs:16, %0" : "=r"(index));
  return &x64_cpus[index];
}

static inline void *x64_physical_pointer(uint64_t physical) {
  return (void *)(x64_hhdm + physical);
}

void x64_cpu_initialize(x64_cpu_t *cpu);
uint64_t x64_virtual_physical(const void *pointer);
bool x64_user_access(uintptr_t address, size_t size, bool writable);
bool x64_user_protect(uintptr_t address, size_t size, bool writable,
                      bool executable);
void x64_syscall_initialize(void);

#endif
