#ifndef _INTERRUPTS_H
#define _INTERRUPTS_H
#include <ctypes.h>

typedef void (*interrupt_entry_t)(void);

bool interrupt_register_entry(unsigned vector, interrupt_entry_t entry);
bool irq_is_valid(unsigned irq);

#define IRQ_BASE_VECTOR 0x20
#define IRQ_TRIGGER_EDGE 0
#define IRQ_TRIGGER_LEVEL 1
#define IRQ_POLARITY_HIGH 0
#define IRQ_POLARITY_LOW 1
// inthandler.c
void inthandler21(int *esp);
void inthandler2c(int *esp);
// pic.c
void init_pic(void);
void pic_disable(void);
void send_eoi(int irq);
// irq.c
void irq_mask_clear(unsigned irq);
void irq_mask_set(unsigned irq);
void irq_configure(unsigned irq, int trigger_mode, int polarity);
int interrupt_controller_uses_apic(void);
#endif
