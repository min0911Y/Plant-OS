#include <arch/x86/io.h>
#include <dos.h>
#include <dma.h>
#include <drivers.h>
#include <limits.h>
#include <net_link.h>
#include <pci.h>

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
  uint16_t receive_offset;
  uint8_t next_transmit;
  bool active;
  net_link_receive_t receive;
  uint32_t receive_dma;
  uint32_t transmit_dma[RTL8139_TX_BUFFERS];
} rtl8139_state_t;

static rtl8139_state_t rtl8139;
static bool rtl8139_interrupt(unsigned irq);
static uint8_t rtl8139_receive_buffer[RTL8139_RX_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t rtl8139_transmit_buffers[RTL8139_TX_BUFFERS]
                                      [RTL8139_TX_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t rtl8139_wrapped_frame[RTL8139_FRAME_MAX];

static bool rtl8139_prepare_dma(void) {
  dma_addr_t address;
  if (!dma_map(rtl8139_receive_buffer, sizeof(rtl8139_receive_buffer),
               UINT_MAX, &address)) {
    return false;
  }
  rtl8139.receive_dma = (uint32_t)address;
  for (unsigned index = 0; index < RTL8139_TX_BUFFERS; index++) {
    if (!dma_map(rtl8139_transmit_buffers[index], RTL8139_TX_BUFFER_BYTES,
                 UINT_MAX, &address)) {
      return false;
    }
    rtl8139.transmit_dma[index] = (uint32_t)address;
  }
  return true;
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
  dma_sync_for_cpu(rtl8139_receive_buffer, sizeof(rtl8139_receive_buffer));
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
  const pci_device_t *device =
      pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
  if (receive == NULL || mac == NULL || device == NULL) {
    logk("rtl8139: PCI device not found\n");
    return false;
  }

  uint32_t port = 0;
  uint8_t irq = pci_interrupt_line(device);
  if (!pci_find_io_bar(device, &port) || port > 0xffffu - RTL8139_CONFIG1 ||
      !irq_is_valid(irq)) {
    logk("rtl8139: invalid I/O port=%08x irq=%d\n", port, irq);
    return false;
  }

  rtl8139.io_base = (uint16_t)port;
  rtl8139.receive = receive;
  rtl8139.receive_offset = 0;
  rtl8139.next_transmit = 0;
  rtl8139.active = false;
  if (!rtl8139_prepare_dma()) {
    logk("rtl8139: DMA buffers are not addressable\n");
    return false;
  }

  pci_command_update(
      device, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0);

  irq_mask_set(irq);
  x86_port_write8(rtl8139.io_base + RTL8139_CONFIG1, 0);
  x86_port_write8(rtl8139.io_base + RTL8139_CMD, 0x10);
  while ((x86_port_read8(rtl8139.io_base + RTL8139_CMD) & 0x10) != 0) {
  }

  for (unsigned i = 0; i < 6; i++) {
    mac[i] = x86_port_read8(rtl8139.io_base + RTL8139_MAC0 + i);
  }
  x86_port_write32(rtl8139.io_base + RTL8139_RBSTART,
                   rtl8139.receive_dma);
  x86_port_write16(rtl8139.io_base + RTL8139_CAPR, 0);
  x86_port_write32(rtl8139.io_base + RTL8139_TCR,
                    (1u << 16) | (3u << 24) | (7u << 8));
  x86_port_write32(rtl8139.io_base + RTL8139_RCR,
                    (8u << 24) | (5u << 13) | (3u << 11) | (7u << 8) |
                        0x0fu);
  x86_port_write16(rtl8139.io_base + RTL8139_IMR, 0x0005);
  x86_port_write8(rtl8139.io_base + RTL8139_CMD, 0x0c);

  if (!irq_register_handler(irq, rtl8139_interrupt, IRQ_SHARED)) {
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
  dma_sync_for_device(buffer, transmit_length);
  x86_port_write32(rtl8139.io_base + RTL8139_TSAD0 + slot * 4,
                   rtl8139.transmit_dma[slot]);
  x86_port_write32(rtl8139.io_base + RTL8139_TSD0 + slot * 4,
                   transmit_length);
  rtl8139.next_transmit = (slot + 1) % RTL8139_TX_BUFFERS;
  return 0;
}

static bool rtl8139_interrupt(unsigned irq) {
  bool reschedule = false;
  uint16_t status = rtl8139_read16(RTL8139_ISR);
  x86_port_write16(rtl8139.io_base + RTL8139_ISR, status);
  if (rtl8139.active && (status & 0x0001) != 0) {
    reschedule = rtl8139_receive();
  }
  return reschedule;
}
