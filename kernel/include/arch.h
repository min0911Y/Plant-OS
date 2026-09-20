#ifndef KERNEL_ARCH_H
#define KERNEL_ARCH_H

#if defined(KERNEL_ARCH_I386)
#include <arch/x86/i386/fpu.h>
#include <arch/x86/i386/task.h>
#elif defined(KERNEL_ARCH_X86_64)
#include <arch/x86/x86_64/task.h>
#else
#error "No Plant OS architecture backend selected"
#endif
#include <boot.h>
#include <stddef.h>

struct mtask;
struct irq_message;

void arch_boot_verify(void);
const boot_info_t *arch_boot_info(void);
/* Highest physical address covered by RAM ranges, including reserved RAM. */
uintptr_t arch_memory_detect(const boot_info_t *boot_info);
/* Sum of usable RAM ranges, excluding address-space holes and reservations. */
uintptr_t arch_memory_available(const boot_info_t *boot_info);
void arch_interrupt_init(void);
void arch_interrupt_init_secondary(void);
bool arch_irq_message(unsigned irq, struct irq_message *message);
void arch_task_state_init(void);
void arch_task_set_kernel_stack(uintptr_t stack_top);
void arch_thread_pointer_set(uintptr_t pointer);
void arch_task_context_init(arch_task_context_t *context, uintptr_t entry);
void arch_task_switch(arch_task_context_t **current_context_slot,
                      arch_task_context_t *next_context,
                      arch_address_space_t next_address_space,
                      struct mtask **scheduler_current_slot,
                      struct mtask *next_task);
__attribute__((noreturn)) void
arch_task_start(arch_task_context_t *next_context,
                arch_address_space_t next_address_space,
                struct mtask **scheduler_current_slot,
                struct mtask *next_task);
__attribute__((noreturn)) void arch_task_interrupt_return(void);
void arch_task_fork_context_init(struct mtask *task);
__attribute__((noreturn)) void arch_task_enter_user(uintptr_t instruction_pointer,
                                                   uintptr_t stack_top,
                                                   uintptr_t argument);

arch_address_space_t arch_address_space_current(void);
arch_address_space_t arch_address_space_kernel(void);
void arch_address_space_activate(arch_address_space_t address_space);
arch_address_space_t
arch_address_space_clone(arch_address_space_t source_address_space);
void arch_address_space_retain(arch_address_space_t address_space);
void arch_address_space_release(arch_address_space_t address_space);
/* The caller holds the VM mapping lock and owns the final address-space ref. */
void arch_address_space_release_locked(arch_address_space_t address_space);
bool arch_address_space_prepare_exec(arch_address_space_t address_space);
bool arch_address_space_share(uintptr_t source, uintptr_t target, size_t size,
                              arch_address_space_t source_address_space,
                              arch_address_space_t target_address_space);
bool arch_address_space_unmap_shared(
    uintptr_t target, size_t size,
    arch_address_space_t target_address_space);
bool arch_address_space_map_user_device(uintptr_t user_address,
                                        uintptr_t physical_address,
                                        size_t size);
void *arch_module_allocate(size_t size);
bool arch_module_protect(void *address, size_t size, bool writable,
                         bool executable);
void arch_module_free(void *address, size_t size);

void arch_fpu_init_cpu(void);
void arch_fpu_flush_cpu(void);
void arch_fpu_reset(struct mtask *task);
void arch_fpu_handle_device_not_available(struct mtask *task);

/* CPU-local scheduler clock. Initialize before starting each CPU's first task;
 * timestamps must never be compared across CPUs. cpu must be the calling CPU;
 * callers pass their known index to avoid an extra APIC lookup on i386. */
void arch_task_clock_init(uint32_t cpu);
uint64_t arch_task_clock_ns(uint32_t cpu);

void arch_cpu_idle(void);
void arch_cpu_relax(void);
bool arch_dma_map(const void *address, size_t size, uint64_t *dma_address);
void arch_dma_sync_for_device(const void *address, size_t size);
void arch_dma_sync_for_cpu(const void *address, size_t size);
void *arch_mmio_map(uint64_t physical_address, size_t size);
__attribute__((noreturn)) void arch_halt(void);

#endif
