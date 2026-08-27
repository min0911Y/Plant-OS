#include <irq.h>

#define X86_EFLAGS_INTERRUPT_ENABLE (1u << 9)

irq_state_t irq_save(void) {
  irq_state_t flags;
  __asm__ volatile("pushfl\n"
                   "popl %0\n"
                   "cli\n"
                   : "=r"(flags)
                   :
                   : "memory");
  return flags;
}

void irq_restore(irq_state_t state) {
  if (state & X86_EFLAGS_INTERRUPT_ENABLE) {
    __asm__ volatile("sti" ::: "memory");
  } else {
    __asm__ volatile("cli" ::: "memory");
  }
}

void irq_enable(void) { __asm__ volatile("sti" ::: "memory"); }
