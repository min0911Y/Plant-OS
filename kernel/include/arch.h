#ifndef KERNEL_ARCH_H
#define KERNEL_ARCH_H

#include <arch/x86/fpu.h>
#include <arch/x86/task.h>
#include <boot.h>
#include <stddef.h>

struct mtask;

void arch_boot_verify(void);
bool arch_boot_initramfs(boot_module_t *module);
void arch_interrupt_init(void);
void arch_interrupt_init_secondary(void);
void arch_task_state_init(void);
void arch_task_set_kernel_stack(uintptr_t stack_top);
void arch_task_context_init(arch_task_context_t *context, uintptr_t entry);
void arch_task_switch(arch_task_context_t **current_context_slot,
                      arch_task_context_t *next_context, uintptr_t next_cr3,
                      struct mtask **scheduler_current_slot,
                      struct mtask *next_task);
__attribute__((noreturn)) void
arch_task_start(arch_task_context_t *next_context, uintptr_t next_cr3,
                struct mtask **scheduler_current_slot,
                struct mtask *next_task);
__attribute__((noreturn)) void arch_task_interrupt_return(void);

#endif
