#include <arch/x86/io.h>
#include <dos.h>
#include <drivers.h>
#include <platform/pc.h>
#define KEYCMD_SENDTO_MOUSE 0xd4
#define MOUSECMD_ENABLE 0xf4
#define MOUSECMD_SAMPLE_RATE 0xf3
#define MOUSECMD_DEVICE_ID 0xf2
#define MOUSE_RESPONSE_ACK 0xfa
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_MOUSE_DATA 0x20
#define PS2_IO_TIMEOUT_NS 100000000ull
mtask *mouse_use_task = NULL;
static uint8_t mouse_packet_bytes;
static void mouse_interrupt(void);

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

lock_t mouse_l;
bool enable_mouse(mouse_decoder_t *mdec) {
  static const uint8_t wheel_sample_rates[] = {200, 100, 80};
  if (!irq_register_handler(12, mouse_interrupt)) {
    return false;
  }
  lock_init(&mouse_l);
  mdec->phase = 1;
  if (!mouse_command(MOUSECMD_ENABLE)) {
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
  uint8_t device_id;
  if (!mouse_command(MOUSECMD_DEVICE_ID) || !mouse_read(&device_id)) {
    return false;
  }
  logk("mouseId=%d\n", device_id);
  return true;
}

void mouse_sleep(mouse_decoder_t *mdec) {
  mouse_use_task = NULL;
  mdec->sleeping = 1;
  return;
}

void mouse_ready(mouse_decoder_t *mdec) {
  mouse_use_task = current_task();
  mouse_packet_bytes = 0;
  mdec->sleeping = 0;
  return;
}
int mouse_decode(mouse_decoder_t *mdec, unsigned char dat) {
  if (mdec->phase == 1) {
    if (dat == 0xfa) { // ACK
      return 0;
    }
    mdec->packet[0] = dat;
    mdec->phase = 2;
    return 0;
  } else if (mdec->phase == 2) {
    mdec->packet[1] = dat;
    mdec->phase = 3;
    return 0;
  } else if (mdec->phase == 3) {
    mdec->packet[2] = dat;
    mdec->phase = 4;
    return 0;
  } else if (mdec->phase == 4) {
    // printk("已经收集了四个字节\n");
    mdec->packet[3] = dat;
    mdec->phase = 1;
    mdec->buttons = mdec->packet[0] & 0x07;
    mdec->x = mdec->packet[1]; // x
    mdec->y = mdec->packet[2]; // y
    if ((mdec->packet[0] & 0x10) != 0) {
      mdec->x |= 0xffffff00;
    }
    if ((mdec->packet[0] & 0x20) != 0) {
      mdec->y |= 0xffffff00;
    }
    mdec->y = -mdec->y; //
    if (mdec->packet[3] == 0xff) {
      mdec->wheel = MOUSE_ROLL_UP;
    } else if (mdec->packet[3] == 0x01) {
      mdec->wheel = MOUSE_ROLL_DOWN;
    } else {
      mdec->wheel = MOUSE_ROLL_NONE;
    }
    return 1;
  }
  return -1;
}
static void mouse_interrupt(void) {
  send_eoi(12);
  uint8_t data = x86_port_read8(PORT_KEYDAT);
  mtask *task = mouse_use_task;
  if (task == NULL || task_get_mouse_fifo(task) == NULL) {
    return;
  }
  fifo8_put(task_get_mouse_fifo(task), data);
  mouse_packet_bytes = (mouse_packet_bytes + 1) & 3u;
  if (mouse_packet_bytes != 0) {
    return;
  }

  task->weight = 5;
  task_run(task);
  if (current_task() != task) {
    mtask_run_now(task);
    task_next();
  }
}
