#include <dos.h>
#include <irq.h>

enum { IRQ_HANDLER_CAPACITY = 256 };

static irq_handler_t irq_handlers[IRQ_HANDLER_CAPACITY];

bool irq_register_handler(unsigned irq, irq_handler_t handler) {
  if (handler == NULL || irq >= IRQ_HANDLER_CAPACITY || !irq_is_valid(irq)) {
    return false;
  }

  irq_state_t state = irq_save();
  bool available = irq_handlers[irq] == NULL || irq_handlers[irq] == handler;
  if (available) {
    irq_handlers[irq] = handler;
  }
  irq_restore(state);
  return available;
}

void irq_dispatch(unsigned irq) {
  if (irq >= IRQ_HANDLER_CAPACITY || irq_handlers[irq] == NULL) {
    send_eoi((int)irq);
    return;
  }
  irq_handlers[irq]();
}
