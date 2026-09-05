#ifndef _INTERRUPTS_H
#define _INTERRUPTS_H
#include <ctypes.h>

/* Return true to request scheduling after every handler has acknowledged its
 * device and the dispatcher has issued the one controller EOI. */
typedef bool (*irq_handler_t)(unsigned irq);
typedef enum { IRQ_EXCLUSIVE, IRQ_SHARED } irq_mode_t;

bool irq_register_handler(unsigned irq, irq_handler_t handler, irq_mode_t mode);
void irq_unregister_handler(unsigned irq, irq_handler_t handler);

typedef struct irq_message {
  uint64_t address;
  uint32_t data;
  unsigned irq;
} irq_message_t;

/* Allocates a dedicated, architecture-routed message interrupt. The device
 * must stop sending before irq_unregister_handler releases the handler. */
bool irq_allocate_message(irq_handler_t handler, irq_message_t *message);
void irq_dispatch(unsigned irq);
bool irq_is_valid(unsigned irq);

#define IRQ_TRIGGER_EDGE 0
#define IRQ_TRIGGER_LEVEL 1
#define IRQ_POLARITY_HIGH 0
#define IRQ_POLARITY_LOW 1
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
