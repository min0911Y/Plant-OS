#include <arch/x86/io.h>
#include <dos.h>
#include <irq.h>
int cnt = 0;
bool rtc_irq(unsigned irq) {
  x86_port_write8(0x70, 0x0C);	// select register C
  x86_port_read8(0x71);		// just throw away contents
  return false;
}
void rtc_start() {
  irq_state_t state = irq_save();
  x86_port_write8(0x70, 0x0B);
  x86_port_write8(0x71, x86_port_read8(0x71) | 0x40);
  irq_restore(state);
}
void rtc_stop() {
  irq_state_t state = irq_save();
  x86_port_write8(0x70, 0x0B);
  x86_port_write8(0x71, x86_port_read8(0x71) &0xBF);
  irq_restore(state);
}
int rtc_init() {
  logk("rtc init\n");
  if (!irq_register_handler(8, rtc_irq, IRQ_EXCLUSIVE)) {
    logk("rtc: invalid interrupt entry\n");
    return -1;
  }
  rtc_stop();
  irq_mask_clear(8);
  return 0;
}
