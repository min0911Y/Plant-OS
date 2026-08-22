#ifndef _INTERRUPTS_H
#define _INTERRUPTS_H
#include <define.h>
#include <perf.h>
#define IRQ_BASE_VECTOR 0x20
#define IRQ_TRIGGER_EDGE 0
#define IRQ_TRIGGER_LEVEL 1
#define IRQ_POLARITY_HIGH 0
#define IRQ_POLARITY_LOW 1
// inthandler.c
void inthandler20(int cs, perf_irq_frame_t *frame);
void inthandler21(int *esp);
void inthandler2c(int *esp);
// pic.c
void init_pic(void);
void pic_disable(void);
void send_eoi(int irq);
// irq.c
void irq_mask_clear(unsigned char irq);
void irq_mask_set(unsigned char irq);
void irq_configure(unsigned char irq, int trigger_mode, int polarity);
int interrupt_controller_uses_apic(void);
#endif
