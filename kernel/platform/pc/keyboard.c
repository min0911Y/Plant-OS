#include <arch/x86/io.h>
#include <dos.h>
#include <drivers.h>
#include <input_device.h>
#include <platform/pc.h>
#define KEYSTA_SEND_NOTREADY 0x02
#define KEYCMD_WRITE_MODE 0x60
#define KBC_MODE 0x47
#define PS2_IO_TIMEOUT_NS 100000000ull
static input_keyboard_t ps2_keyboard;
static uint16_t prefix;
static unsigned pause_bytes;
static bool keyboard_interrupt(unsigned irq);
bool ps2_wait_input_empty(void) {
  uint64_t started = monotonic_time_ns();
  while (monotonic_time_ns() - started < PS2_IO_TIMEOUT_NS) {
    if ((x86_port_read8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) {
      return true;
    }
  }
  return false;
}

bool init_keyboard(void) {
  if (!irq_register_handler(1, keyboard_interrupt, IRQ_EXCLUSIVE)) {
    return false;
  }
  if (!ps2_wait_input_empty()) {
    return false;
  }
  x86_port_write8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
  if (!ps2_wait_input_empty()) {
    return false;
  }
  x86_port_write8(PORT_KEYDAT, KBC_MODE);
  return true;
}
static bool keyboard_interrupt(unsigned irq) {
  uint8_t data = x86_port_read8(PORT_KEYDAT);
  if (pause_bytes != 0) {
    if (--pause_bytes == 0) {
      bool reschedule = input_keyboard_event(&ps2_keyboard, 0x145, true);
      return input_keyboard_event(&ps2_keyboard, 0x145, false) || reschedule;
    }
    return false;
  }
  if (data == 0xe1) {
    pause_bytes = 5;
    prefix = 0;
    return false;
  }
  if (data == 0xe0) {
    prefix = 0x100;
    return false;
  }
  uint16_t code = prefix | (data & 0x7f);
  prefix = 0;
  return input_keyboard_event(&ps2_keyboard, code, (data & 0x80) == 0);
}
