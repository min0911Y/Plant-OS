#include <arch.h>
#include <arch/x86/i386/control.h>
#include <arch/x86/i386/memory.h>
#include <limits.h>

arch_address_space_t arch_address_space_current(void) {
  return x86_cr3_read() & 0xfffff000u;
}

arch_address_space_t arch_address_space_kernel(void) {
  return I386_KERNEL_PAGE_DIRECTORY;
}

void arch_address_space_activate(arch_address_space_t address_space) {
  x86_cr3_write(address_space);
}

bool arch_dma_map(const void *address, size_t size, uint64_t *dma_address) {
  uintptr_t start = (uintptr_t)address;
  if (dma_address == NULL || (size != 0 && size - 1 > UINT_MAX - start)) {
    return false;
  }
  *dma_address = start;
  return true;
}

void arch_dma_sync_for_device(const void *address, size_t size) {
  (void)address;
  (void)size;
  asm volatile("" : : : "memory");
}

void arch_dma_sync_for_cpu(const void *address, size_t size) {
  (void)address;
  (void)size;
  asm volatile("" : : : "memory");
}

void arch_cpu_idle(void) { asm volatile("sti; hlt" : : : "memory"); }

void arch_cpu_relax(void) { asm volatile("pause" : : : "memory"); }

__attribute__((noreturn)) void arch_halt(void) {
  for (;;) {
    asm volatile("cli; hlt" : : : "memory");
  }
}
