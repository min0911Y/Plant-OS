#include <arch/x86/interrupt.h>
#include <arch/x86/io.h>
#include <dos.h>
#include <drivers.h>
#include <net_link.h>
#include <pci.h>

#define PCNET_VENDOR_ID 0x1022
#define PCNET_DEVICE_ID 0x2000

#define PCNET_CSR0 0
#define PCNET_CSR1 1
#define PCNET_CSR2 2
#define PCNET_CSR4 4
#define PCNET_BCR20 20

#define PCNET_APROM0 0x00
#define PCNET_RDP 0x10
#define PCNET_RAP 0x12
#define PCNET_RESET 0x14
#define PCNET_BDP 0x16

#define PCNET_RING_COUNT 8
#define PCNET_BUFFER_BYTES 2048
#define PCNET_FRAME_MAX 1514

#define PCNET_DESC_OWN 0x80000000u
#define PCNET_DESC_ERR 0x40000000u
#define PCNET_DESC_STP 0x02000000u
#define PCNET_DESC_ENP 0x01000000u
#define PCNET_DESC_RX_BUFFER 0x0000f7ffu
#define PCNET_DESC_TX_FLAGS 0x8300f000u

typedef struct {
  uint16_t mode;
  uint8_t tlen;
  uint8_t rlen;
  uint8_t mac[6];
  uint16_t reserved;
  uint64_t filter;
  uint32_t receive_ring;
  uint32_t transmit_ring;
} __attribute__((packed)) pcnet_init_block_t;

typedef struct {
  uint32_t address;
  uint32_t flags;
  uint32_t flags2;
  uint32_t available;
} __attribute__((packed)) pcnet_descriptor_t;

typedef struct {
  uint16_t io_base;
  uint8_t irq;
  uint8_t next_receive;
  uint8_t next_transmit;
  bool active;
  net_link_receive_t receive;
} pcnet_state_t;

static pcnet_state_t pcnet;
static pcnet_init_block_t pcnet_init_block __attribute__((aligned(16)));
static pcnet_descriptor_t
    pcnet_receive_descriptors[PCNET_RING_COUNT] __attribute__((aligned(16)));
static pcnet_descriptor_t
    pcnet_transmit_descriptors[PCNET_RING_COUNT] __attribute__((aligned(16)));
static uint8_t pcnet_receive_buffers[PCNET_RING_COUNT][PCNET_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t pcnet_transmit_buffers[PCNET_RING_COUNT][PCNET_BUFFER_BYTES]
    __attribute__((aligned(16)));

static void pcnet_select_csr(uint16_t csr) {
  x86_port_write16(pcnet.io_base + PCNET_RAP, csr);
}

static uint16_t pcnet_read_csr(uint16_t csr) {
  pcnet_select_csr(csr);
  return x86_port_read16(pcnet.io_base + PCNET_RDP);
}

static void pcnet_write_csr(uint16_t csr, uint16_t value) {
  pcnet_select_csr(csr);
  x86_port_write16(pcnet.io_base + PCNET_RDP, value);
}

static void pcnet_rearm_receive(pcnet_descriptor_t *descriptor) {
  descriptor->flags2 = 0;
  descriptor->available = 0;
  descriptor->flags = PCNET_DESC_OWN | PCNET_DESC_RX_BUFFER;
}

static void pcnet_prepare_rings(const uint8_t mac[6]) {
  for (unsigned i = 0; i < PCNET_RING_COUNT; i++) {
    pcnet_transmit_descriptors[i].address =
        (uint32_t)(uintptr_t)pcnet_transmit_buffers[i];
    pcnet_transmit_descriptors[i].flags = PCNET_DESC_RX_BUFFER;
    pcnet_transmit_descriptors[i].flags2 = 0;
    pcnet_transmit_descriptors[i].available = 0;

    pcnet_receive_descriptors[i].address =
        (uint32_t)(uintptr_t)pcnet_receive_buffers[i];
    pcnet_rearm_receive(&pcnet_receive_descriptors[i]);
  }

  memset(&pcnet_init_block, 0, sizeof(pcnet_init_block));
  pcnet_init_block.tlen = 3u << 4;
  pcnet_init_block.rlen = 3u << 4;
  memcpy(pcnet_init_block.mac, mac, sizeof(pcnet_init_block.mac));
  pcnet_init_block.receive_ring = (uint32_t)(uintptr_t)pcnet_receive_descriptors;
  pcnet_init_block.transmit_ring =
      (uint32_t)(uintptr_t)pcnet_transmit_descriptors;
}

static bool pcnet_receive(void) {
  bool reschedule = false;
  for (;;) {
    pcnet_descriptor_t *descriptor =
        &pcnet_receive_descriptors[pcnet.next_receive];
    uint32_t flags = descriptor->flags;
    if ((flags & PCNET_DESC_OWN) != 0) {
      return reschedule;
    }

    uint16_t length = (uint16_t)(descriptor->flags2 & 0x0fffu);
    if ((flags & (PCNET_DESC_ERR | PCNET_DESC_STP | PCNET_DESC_ENP)) ==
            (PCNET_DESC_STP | PCNET_DESC_ENP) &&
        length >= 4 && length - 4 >= 14 && length - 4 <= PCNET_FRAME_MAX) {
      reschedule |=
          pcnet.receive(pcnet_receive_buffers[pcnet.next_receive], length - 4);
    }

    pcnet_rearm_receive(descriptor);
    pcnet.next_receive = (pcnet.next_receive + 1) % PCNET_RING_COUNT;
  }
}

bool pcnet_link_start(net_link_receive_t receive, uint8_t mac[6]) {
  const pci_device_t *device =
      pci_find_device(PCNET_VENDOR_ID, PCNET_DEVICE_ID);
  if (receive == NULL || mac == NULL || device == NULL) {
    logk("pcnet: PCI device not found\n");
    return false;
  }

  uint32_t port = 0;
  uint8_t irq = pci_interrupt_line(device);
  if (!pci_find_io_bar(device, &port) || port > 0xffffu - PCNET_BDP ||
      !irq_is_valid(irq)) {
    logk("pcnet: invalid I/O port=%08x irq=%d\n", port, irq);
    return false;
  }

  pcnet.io_base = (uint16_t)port;
  pcnet.irq = irq;
  pcnet.receive = receive;
  pcnet.next_receive = 0;
  pcnet.next_transmit = 0;
  pcnet.active = false;

  pci_command_enable(device, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                                 PCI_COMMAND_BUS_MASTER);

  irq_mask_set(irq);
  x86_port_read16(pcnet.io_base + PCNET_RESET);
  x86_port_write16(pcnet.io_base + PCNET_RESET, 0);
  x86_port_write16(pcnet.io_base + PCNET_RAP, PCNET_BCR20);
  x86_port_write16(pcnet.io_base + PCNET_BDP, 0x0102);
  pcnet_write_csr(PCNET_CSR0, 0x0004);

  for (unsigned i = 0; i < 6; i++) {
    mac[i] = x86_port_read8(pcnet.io_base + PCNET_APROM0 + i);
  }
  pcnet_prepare_rings(mac);

  pcnet_write_csr(PCNET_CSR1, (uint16_t)(uintptr_t)&pcnet_init_block);
  pcnet_write_csr(PCNET_CSR2, (uint16_t)((uintptr_t)&pcnet_init_block >> 16));
  pcnet_write_csr(PCNET_CSR0, 0x0041);
  pcnet_write_csr(PCNET_CSR4, pcnet_read_csr(PCNET_CSR4) | 0x0c00);
  pcnet_write_csr(PCNET_CSR0, 0x0042);

  if (!interrupt_register_entry(IRQ_BASE_VECTOR + irq, PCNET_ASM_INTHANDLER)) {
    logk("pcnet: unable to register IRQ %d\n", irq);
    return false;
  }
  pcnet.active = true;
  irq_configure(irq, IRQ_TRIGGER_LEVEL, IRQ_POLARITY_LOW);
  irq_mask_clear(irq);
  return true;
}

int pcnet_link_transmit(const uint8_t *frame, uint16_t length) {
  if (!pcnet.active || frame == NULL || length < 14 || length > PCNET_FRAME_MAX) {
    return -1;
  }

  pcnet_descriptor_t *descriptor =
      &pcnet_transmit_descriptors[pcnet.next_transmit];
  if ((descriptor->flags & PCNET_DESC_OWN) != 0) {
    return -1;
  }

  uint16_t transmit_length = length < 60 ? 60 : length;
  uint8_t *buffer = pcnet_transmit_buffers[pcnet.next_transmit];
  memcpy(buffer, frame, length);
  if (transmit_length > length) {
    memset(buffer + length, 0, transmit_length - length);
  }

  descriptor->flags2 = 0;
  descriptor->available = 0;
  descriptor->flags =
      PCNET_DESC_TX_FLAGS | ((uint16_t)(-(int)transmit_length) & 0x0fffu);
  pcnet.next_transmit = (pcnet.next_transmit + 1) % PCNET_RING_COUNT;
  pcnet_write_csr(PCNET_CSR0, 0x0048);
  return 0;
}

void PCNET_IRQ(void) {
  bool reschedule = false;
  uint16_t status = pcnet_read_csr(PCNET_CSR0);
  pcnet_write_csr(PCNET_CSR0, status);
  if (pcnet.active && (status & 0x0400) != 0) {
    reschedule = pcnet_receive();
  }
  if (pcnet.active) {
    send_eoi(pcnet.irq);
  }
  if (reschedule) {
    task_next();
  }
}
