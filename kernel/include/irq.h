#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

#include <stddef.h>

typedef uintptr_t irq_state_t;

irq_state_t irq_save(void);
void irq_restore(irq_state_t state);
void irq_enable(void);

#endif
