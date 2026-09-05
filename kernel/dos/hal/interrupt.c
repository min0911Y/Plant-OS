#include <dos.h>
#include <irq.h>
#include <limits.h>

enum { IRQ_HANDLER_CAPACITY = 256 };

typedef struct irq_entry {
  irq_handler_t handler;
  struct irq_entry *next;
  unsigned references;
} irq_entry_t;

static struct {
  irq_entry_t first;
  irq_mode_t mode;
} irq_handlers[IRQ_HANDLER_CAPACITY];

bool irq_register_handler(unsigned irq, irq_handler_t handler,
                          irq_mode_t mode) {
  if (handler == NULL || irq >= IRQ_HANDLER_CAPACITY || !irq_is_valid(irq) ||
      (mode != IRQ_EXCLUSIVE && mode != IRQ_SHARED)) {
    return false;
  }

  irq_state_t state = irq_save();
  irq_entry_t *entry = &irq_handlers[irq].first;
  if (entry->handler == NULL) {
    /* Early ISA registration must work before the kernel heap exists. */
    entry->handler = handler;
    entry->references = 1;
    irq_handlers[irq].mode = mode;
    irq_restore(state);
    return true;
  }
  for (;;) {
    if (entry->handler == handler) {
      bool valid = irq_handlers[irq].mode == mode && entry->references != UINT_MAX;
      if (valid) {
        entry->references++;
      }
      irq_restore(state);
      return valid;
    }
    if (entry->next == NULL) {
      break;
    }
    entry = entry->next;
  }
  bool available = mode == IRQ_SHARED && irq_handlers[irq].mode == IRQ_SHARED;
  irq_entry_t *added = available ? malloc(sizeof(*added)) : NULL;
  if (added != NULL) {
    *added = (irq_entry_t){.handler = handler, .references = 1};
    entry->next = added;
  }
  irq_restore(state);
  return added != NULL;
}

bool irq_allocate_message(irq_handler_t handler, irq_message_t *message) {
  if (handler == NULL || message == NULL) {
    return false;
  }
  irq_state_t state = irq_save();
  for (unsigned irq = 0; irq < IRQ_HANDLER_CAPACITY; irq++) {
    irq_entry_t *entry = &irq_handlers[irq].first;
    if (entry->handler != NULL || !arch_irq_message(irq, message)) {
      continue;
    }
    *entry = (irq_entry_t){.handler = handler, .references = 1};
    irq_handlers[irq].mode = IRQ_EXCLUSIVE;
    irq_restore(state);
    return true;
  }
  irq_restore(state);
  return false;
}

void irq_unregister_handler(unsigned irq, irq_handler_t handler) {
  if (irq >= IRQ_HANDLER_CAPACITY || handler == NULL) {
    return;
  }
  irq_state_t state = irq_save();
  irq_entry_t *first = &irq_handlers[irq].first;
  irq_entry_t *previous = NULL;
  for (irq_entry_t *entry = first; entry != NULL; entry = entry->next) {
    if (entry->handler != handler) {
      previous = entry;
      continue;
    }
    if (--entry->references != 0) {
      break;
    }
    if (previous != NULL) {
      previous->next = entry->next;
      free(entry);
    } else if (entry->next != NULL) {
      irq_entry_t *next = entry->next;
      *entry = *next;
      free(next);
    } else {
      *entry = (irq_entry_t){0};
      if (irq_is_valid(irq)) {
        irq_mask_set(irq);
      }
    }
    break;
  }
  irq_restore(state);
}

void irq_dispatch(unsigned irq) {
  bool reschedule = false;
  if (irq < IRQ_HANDLER_CAPACITY) {
    for (irq_entry_t *entry = &irq_handlers[irq].first;
         entry != NULL && entry->handler != NULL; entry = entry->next) {
      reschedule |= entry->handler(irq);
    }
  }
  send_eoi((int)irq);
  if (reschedule) {
    task_next();
  }
}
