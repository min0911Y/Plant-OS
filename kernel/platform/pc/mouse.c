#include <arch/x86/io.h>
#include <dos.h>
#include <drivers.h>
#include <input_device.h>
#include <platform/pc.h>
#define KEYCMD_SENDTO_MOUSE 0xd4
#define MOUSECMD_ENABLE 0xf4
#define MOUSECMD_DISABLE 0xf5
#define MOUSECMD_SAMPLE_RATE 0xf3
#define MOUSECMD_DEVICE_ID 0xf2
#define MOUSE_RESPONSE_ACK 0xfa
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_MOUSE_DATA 0x20
#define PS2_IO_TIMEOUT_NS 100000000ull
static uint8_t packet[4], packet_bytes, mouse_id;
static input_pointer_t pointer;
static bool mouse_interrupt(unsigned irq);

static bool mouse_read(uint8_t *value) {
  uint64_t started = monotonic_time_ns();
  while (monotonic_time_ns() - started < PS2_IO_TIMEOUT_NS) {
    uint8_t status = x86_port_read8(PORT_KEYSTA);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0) {
      continue;
    }
    uint8_t data = x86_port_read8(PORT_KEYDAT);
    if (status & PS2_STATUS_MOUSE_DATA) {
      *value = data;
      return true;
    }
  }
  return false;
}

static bool mouse_command(uint8_t command) {
  if (!ps2_wait_input_empty()) {
    return false;
  }
  x86_port_write8(PORT_KEYCMD, KEYCMD_SENDTO_MOUSE);
  if (!ps2_wait_input_empty()) {
    return false;
  }
  x86_port_write8(PORT_KEYDAT, command);
  uint8_t response;
  return mouse_read(&response) && response == MOUSE_RESPONSE_ACK;
}

bool enable_mouse(void) {
  static const uint8_t wheel_sample_rates[] = {200, 100, 80};
  if (!irq_register_handler(12, mouse_interrupt, IRQ_EXCLUSIVE)) {
    return false;
  }
  if (!mouse_command(MOUSECMD_DISABLE)) {
    return false;
  }
  for (size_t index = 0;
       index < sizeof(wheel_sample_rates) / sizeof(wheel_sample_rates[0]);
       index++) {
    if (!mouse_command(MOUSECMD_SAMPLE_RATE) ||
        !mouse_command(wheel_sample_rates[index])) {
      return false;
    }
  }
  if (!mouse_command(MOUSECMD_DEVICE_ID) || !mouse_read(&mouse_id) ||
      !mouse_command(MOUSECMD_ENABLE)) {
    return false;
  }
  logk("mouseId=%d\n", mouse_id);
  return true;
}

static bool mouse_interrupt(unsigned irq) {
  uint8_t byte = x86_port_read8(PORT_KEYDAT);
  if (packet_bytes == 0 && !(byte & 8)) {
    return false;
  }
  packet[packet_bytes++] = byte;
  unsigned packet_size = mouse_id == 3 || mouse_id == 4 ? 4 : 3;
  if (packet_bytes != packet_size) {
    return false;
  }
  packet_bytes = 0;
  if (packet[0] & 0xc0) {
    return false;
  }
  mouse_event_t event = {
      .x = (int)packet[1] - ((packet[0] & 0x10) ? 256 : 0),
      .y = -((int)packet[2] - ((packet[0] & 0x20) ? 256 : 0)),
      .wheel = mouse_id == 3   ? -(int8_t)packet[3]
               : mouse_id == 4 ? -(int8_t)(packet[3] << 4) / 16
                               : 0,
      .buttons =
          (packet[0] & 7) | (mouse_id == 4 ? (packet[3] & 0x30) >> 1 : 0),
  };
  return input_mouse_event(&pointer, &event);
}
