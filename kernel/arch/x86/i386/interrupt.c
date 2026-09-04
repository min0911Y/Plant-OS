#include <arch/x86/i386/interrupt.h>
#include <dos.h>
#include <interrupts.h>

void x86_irq_dispatch(unsigned irq, x86_interrupt_frame_t *frame) {
#ifdef KERNEL_PERF
  if (irq == 0) {
    perf_sample(frame->eip, frame->ebp, (frame->cs & 3u) == 3u);
  }
#else
  (void)frame;
#endif

  irq_dispatch(irq);
  if (irq <= 1) {
    signal_deal();
  }
}
