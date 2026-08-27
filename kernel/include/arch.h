#ifndef KERNEL_ARCH_H
#define KERNEL_ARCH_H

#include <stddef.h>

void arch_interrupt_init(void);
void arch_task_state_init(void);
void arch_task_set_kernel_stack(uintptr_t stack_top);

#endif
