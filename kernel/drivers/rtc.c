#include <dos.h>
void asm_rtc_handler();
int cnt = 0;
void rtc_irq() {
  send_eoi(8);
  io_out8(0x70, 0x0C);	// select register C
  io_in8(0x71);		// just throw away contents
}
void rtc_start() {
  io_cli();
  io_out8(0x70, 0x0B);
  io_out8(0x71, io_in8(0x71) | 0x40);
  io_sti();
}
void rtc_stop() {
  io_cli();
  io_out8(0x70, 0x0B);
  io_out8(0x71, io_in8(0x71) &0xBF);
  io_sti();
}
int rtc_init() {
  logk("rtc init\n");
  register_intr_handler(0x28,asm_rtc_handler);
  rtc_stop();
  irq_mask_clear(8);
}