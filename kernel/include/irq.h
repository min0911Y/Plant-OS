#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

typedef unsigned int irq_state_t;

irq_state_t irq_save(void);
void irq_restore(irq_state_t state);

#endif
