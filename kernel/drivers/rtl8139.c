#include <arch/x86/interrupt.h>
#include <arch/x86/io.h>
#include <dos.h>
#include <drivers.h>
#include <net_link.h>

#define RTL8139_VENDOR_ID 0x10ec
#define RTL8139_DEVICE_ID 0x8139

#define RTL8139_MAC0 0x00
#define RTL8139_TSD0 0x10
#define RTL8139_TSAD0 0x20
#define RTL8139_RBSTART 0x30
#define RTL8139_CAPR 0x38
#define RTL8139_CMD 0x37
#define RTL8139_IMR 0x3c
#define RTL8139_ISR 0x3e
#define RTL8139_TCR 0x40
#define RTL8139_RCR 0x44
#define RTL8139_CONFIG1 0x52

#define RTL8139_RX_RING_BYTES 8192
#define RTL8139_RX_BUFFER_BYTES (RTL8139_RX_RING_BYTES + 16 + 1500)
#define RTL8139_TX_BUFFER_BYTES 1536
#define RTL8139_TX_BUFFERS 4
#define RTL8139_FRAME_MAX 1514
#define RTL8139_CMD_RX_EMPTY 0x01
#define RTL8139_RX_OK 0x0001
#define RTL8139_TX_OWN 0x00002000u

typedef struct {
  uint16_t io_base;
  uint8_t irq;
  uint16_t receive_offset;
  uint8_t next_transmit;
  bool active;
  net_link_receive_t receive;
} rtl8139_state_t;

static rtl8139_state_t rtl8139;
static uint8_t rtl8139_receive_buffer[RTL8139_RX_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t rtl8139_transmit_buffers[RTL8139_TX_BUFFERS]
                                      [RTL8139_TX_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t rtl8139_wrapped_frame[RTL8139_FRAME_MAX];

static bool rtl8139_probe(uint8_t *bus, uint8_t *device, uint8_t *function) {
  *bus = 0xff;
  *device = 0xff;
  *function = 0xff;
  PCI_GET_DEVICE(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, bus, device, function);
  return *bus != 0xff;
}

static uint16_t rtl8139_read16(uint16_t offset) {
  return x86_port_read16(rtl8139.io_base + offset);
}

static bool rtl8139_deliver_frame(uint16_t length) {
  uint16_t frame_length = length - 4;
  uint16_t offset = rtl8139.receive_offset + 4;
  if (offset + frame_length <= RTL8139_RX_RING_BYTES) {
    return rtl8139.receive(rtl8139_receive_buffer + offset, frame_length);
  }

  uint16_t first = RTL8139_RX_RING_BYTES - offset;
  memcpy(rtl8139_wrapped_frame, rtl8139_receive_buffer + offset, first);
  memcpy(rtl8139_wrapped_frame + first, rtl8139_receive_buffer,
         frame_length - first);
  return rtl8139.receive(rtl8139_wrapped_frame, frame_length);
}

static bool rtl8139_receive(void) {
  bool reschedule = false;
  while ((x86_port_read8(rtl8139.io_base + RTL8139_CMD) &
          RTL8139_CMD_RX_EMPTY) == 0) {
    uint16_t status;
    uint16_t length;
    uint8_t *header = rtl8139_receive_buffer + rtl8139.receive_offset;
    memcpy(&status, header, sizeof(status));
    memcpy(&length, header + sizeof(status), sizeof(length));

    if ((status & RTL8139_RX_OK) == 0 || length < 4 ||
        length - 4 > RTL8139_FRAME_MAX) {
      x86_port_write8(rtl8139.io_base + RTL8139_CMD, 0x10);
      while ((x86_port_read8(rtl8139.io_base + RTL8139_CMD) & 0x10) != 0) {
      }
      rtl8139.receive_offset = 0;
      x86_port_write16(rtl8139.io_base + RTL8139_CAPR, 0);
      return reschedule;
    }

    reschedule |= rtl8139_deliver_frame(length);
    rtl8139.receive_offset =
        (rtl8139.receive_offset + length + 4 + 3) & ~((uint16_t)3);
    rtl8139.receive_offset %= RTL8139_RX_RING_BYTES;
    x86_port_write16(rtl8139.io_base + RTL8139_CAPR,
                     rtl8139.receive_offset - 16);
  }
  return reschedule;
}

bool rtl8139_link_start(net_link_receive_t receive, uint8_t mac[6]) {
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  if (receive == NULL || mac == NULL ||
      !rtl8139_probe(&bus, &device, &function)) {
    logk("rtl8139: PCI device not found\n");
    return false;
  }

  uint32_t port = pci_get_port_base(bus, device, function);
  uint8_t irq = pci_get_drive_irq(bus, device, function);
  if (port == 0 || port > 0xffffu - RTL8139_CONFIG1 || !irq_is_valid(irq)) {
    logk("rtl8139: invalid I/O port=%08x irq=%d\n", port, irq);
    return false;
  }

  rtl8139.io_base = (uint16_t)port;
  rtl8139.irq = irq;
  rtl8139.receive = receive;
  rtl8139.receive_offset = 0;
  rtl8139.next_transmit = 0;
  rtl8139.active = false;

  uint32_t command_status = pci_read_command_status(bus, device, function);
  command_status = (command_status & 0xffff0000u) |
                   ((command_status & 0xffffu) | 0x0007u);
  pci_write_command_status(bus, device, function, command_status);

  irq_mask_set(irq);
  x86_port_write8(rtl8139.io_base + RTL8139_CONFIG1, 0);
  x86_port_write8(rtl8139.io_base + RTL8139_CMD, 0x10);
  while ((x86_port_read8(rtl8139.io_base + RTL8139_CMD) & 0x10) != 0) {
  }

  for (unsigned i = 0; i < 6; i++) {
    mac[i] = x86_port_read8(rtl8139.io_base + RTL8139_MAC0 + i);
  }
  x86_port_write32(rtl8139.io_base + RTL8139_RBSTART,
                   (uint32_t)(uintptr_t)rtl8139_receive_buffer);
  x86_port_write16(rtl8139.io_base + RTL8139_CAPR, 0);
  x86_port_write32(rtl8139.io_base + RTL8139_TCR,
                    (1u << 16) | (3u << 24) | (7u << 8));
  x86_port_write32(rtl8139.io_base + RTL8139_RCR,
                    (8u << 24) | (5u << 13) | (3u << 11) | (7u << 8) |
                        0x0fu);
  x86_port_write16(rtl8139.io_base + RTL8139_IMR, 0x0005);
  x86_port_write8(rtl8139.io_base + RTL8139_CMD, 0x0c);

  if (!interrupt_register_entry(IRQ_BASE_VECTOR + irq,
                                RTL8139_ASM_INTHANDLER)) {
    logk("rtl8139: unable to register IRQ %d\n", irq);
    return false;
  }
  rtl8139.active = true;
  irq_configure(irq, IRQ_TRIGGER_LEVEL, IRQ_POLARITY_LOW);
  irq_mask_clear(irq);
  return true;
}

int rtl8139_link_transmit(const uint8_t *frame, uint16_t length) {
  if (!rtl8139.active || frame == NULL || length < 14 ||
      length > RTL8139_FRAME_MAX) {
    return -1;
  }

  unsigned slot = rtl8139.next_transmit;
  if ((x86_port_read32(rtl8139.io_base + RTL8139_TSD0 + slot * 4) &
       RTL8139_TX_OWN) != 0) {
    return -1;
  }

  uint16_t transmit_length = length < 60 ? 60 : length;
  uint8_t *buffer = rtl8139_transmit_buffers[slot];
  memcpy(buffer, frame, length);
  if (transmit_length > length) {
    memset(buffer + length, 0, transmit_length - length);
  }
  x86_port_write32(rtl8139.io_base + RTL8139_TSAD0 + slot * 4,
                   (uint32_t)(uintptr_t)buffer);
  x86_port_write32(rtl8139.io_base + RTL8139_TSD0 + slot * 4,
                   transmit_length);
  rtl8139.next_transmit = (slot + 1) % RTL8139_TX_BUFFERS;
  return 0;
}

void RTL8139_IRQ(void) {
  bool reschedule = false;
  uint16_t status = rtl8139_read16(RTL8139_ISR);
  x86_port_write16(rtl8139.io_base + RTL8139_ISR, status);
  if (rtl8139.active && (status & 0x0001) != 0) {
    reschedule = rtl8139_receive();
  }
  if (rtl8139.active) {
    send_eoi(rtl8139.irq);
  }
  if (reschedule) {
    task_next();
  }
}
