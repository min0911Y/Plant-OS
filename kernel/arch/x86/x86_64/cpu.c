#include "boot.h"
#include <dos.h>
#include <irq.h>

irq_state_t irq_save(void) {
  irq_state_t flags;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  return flags;
}
void irq_restore(irq_state_t flags) {
  if (flags & (1u << 9))
    __asm__ volatile("sti" : : : "memory");
  else
    __asm__ volatile("cli" : : : "memory");
}
void irq_enable(void) { __asm__ volatile("sti" : : : "memory"); }
void arch_cpu_idle(void) { __asm__ volatile("sti; hlt" : : : "memory"); }
void arch_cpu_relax(void) { __asm__ volatile("pause" : : : "memory"); }
void arch_halt(void) {
  for (;;)
    __asm__ volatile("cli; hlt" : : : "memory");
}

bool arch_dma_map(const void *address, size_t size, uint64_t *physical) {
  if (!physical || !size || (uintptr_t)address > UINTPTR_MAX - (size - 1))
    return false;
  uint64_t first = x64_virtual_physical(address);
  if (first == UINT64_MAX)
    return false;
  for (size_t offset = 4096 - ((uintptr_t)address & 4095); offset < size;
       offset += 4096) {
    if (x64_virtual_physical((const uint8_t *)address + offset) !=
        first + offset)
      return false;
  }
  *physical = first;
  return true;
}
void arch_dma_sync_for_device(const void *address, size_t size) {
  (void)address;
  (void)size;
  __asm__ volatile("sfence" : : : "memory");
}
void arch_dma_sync_for_cpu(const void *address, size_t size) {
  (void)address;
  (void)size;
  __asm__ volatile("lfence" : : : "memory");
}

void x64_cpu_initialize(x64_cpu_t *cpu) {
  x64_msr_write(0xc0000101, (uintptr_t)cpu);
  x64_msr_write(0xc0000102, 0);
  x64_msr_write(0xc0000100, 0);
  arch_fpu_init_cpu();
  x64_tlb_initialize();
  x64_syscall_initialize();
}
