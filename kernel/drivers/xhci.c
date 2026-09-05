#include <dma.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <pci.h>
#include <stdint.h>
#include <usb.h>

/* xHCI 1.x, primary interrupter, one segment per ring. All device work is
 * serialized by the service task; the IRQ only consumes events and wakes it. */
enum {
  XHCI_PAGE_SIZE = 4096,
  XHCI_RING_SIZE = XHCI_PAGE_SIZE / 16,
  XHCI_TIMEOUT_MS = 1000,
  XHCI_TRANSFER_TIMEOUT_MS = 5000,
  XHCI_COMMAND = 0x00,
  XHCI_STATUS = 0x04,
  XHCI_PAGE_SIZES = 0x08,
  XHCI_COMMAND_RING = 0x18,
  XHCI_DCBAA = 0x30,
  XHCI_CONFIG = 0x38,
  XHCI_RUN = 1u << 0,
  XHCI_RESET = 1u << 1,
  XHCI_INTERRUPTS = 1u << 2,
  XHCI_ERROR_INTERRUPTS = 1u << 3,
  XHCI_HALTED = 1u << 0,
  XHCI_HOST_ERROR = 1u << 2,
  XHCI_EVENT_PENDING = 1u << 3,
  XHCI_PORT_PENDING = 1u << 4,
  XHCI_NOT_READY = 1u << 11,
  XHCI_CONTROLLER_ERROR = 1u << 12,
  XHCI_PORT_CONNECTED = 1u << 0,
  XHCI_PORT_ENABLED = 1u << 1,
  XHCI_PORT_RESET = 1u << 4,
  XHCI_PORT_POWER = 1u << 9,
  XHCI_PORT_CONNECT_CHANGE = 1u << 17,
  XHCI_PORT_CHANGES = 0x7fu << 17,
  XHCI_PORT_WRITABLE = (1u << 9) | (3u << 14) | (7u << 25),
  XHCI_PORT_WARM_RESET = 1u << 31,
  XHCI_IMAN = 0x00,
  XHCI_IMOD = 0x04,
  XHCI_ERST_SIZE = 0x08,
  XHCI_ERST_BASE = 0x10,
  XHCI_EVENT_DEQUEUE = 0x18,
  XHCI_CYCLE = 1u << 0,
  XHCI_TOGGLE_CYCLE = 1u << 1,
  XHCI_SHORT_INTERRUPT = 1u << 2,
  XHCI_COMPLETION_INTERRUPT = 1u << 5,
  XHCI_IMMEDIATE_DATA = 1u << 6,
  XHCI_DIRECTION_IN = 1u << 16,
  XHCI_NORMAL = 1,
  XHCI_SETUP = 2,
  XHCI_DATA = 3,
  XHCI_STATUS_STAGE = 4,
  XHCI_LINK = 6,
  XHCI_ENABLE_SLOT = 9,
  XHCI_DISABLE_SLOT = 10,
  XHCI_ADDRESS_DEVICE = 11,
  XHCI_CONFIGURE_ENDPOINT = 12,
  XHCI_EVALUATE_CONTEXT = 13,
  XHCI_RESET_ENDPOINT = 14,
  XHCI_STOP_ENDPOINT = 15,
  XHCI_SET_DEQUEUE = 16,
  XHCI_NOOP = 23,
  XHCI_TRANSFER_EVENT = 32,
  XHCI_COMMAND_EVENT = 33,
  XHCI_PORT_EVENT = 34,
  XHCI_HOST_EVENT = 37,
  XHCI_SUCCESS = 1,
  XHCI_SHORT_PACKET = 13,
};

typedef struct {
  uint64_t parameter;
  uint32_t status, control;
} xhci_trb_t;

typedef struct {
  uint64_t address;
  uint32_t count, reserved;
} xhci_event_segment_t;

typedef struct {
  void *allocation, *data;
  dma_addr_t address;
  unsigned size;
} xhci_dma_t;

typedef struct {
  xhci_dma_t memory;
  unsigned index, cycle;
} xhci_ring_t;

typedef struct {
  dma_addr_t address;
  unsigned code, residual, slot;
  bool active;
} xhci_completion_t;

typedef struct {
  xhci_ring_t ring;
  xhci_dma_t buffer;
  unsigned capacity;
  xhci_completion_t pending;
  usb_interrupt_callback_t callback;
  void *context;
} xhci_endpoint_t;

typedef struct xhci_host xhci_host_t;
typedef struct {
  usb_device_t usb;
  xhci_host_t *host;
  xhci_dma_t input, output;
  xhci_endpoint_t *endpoints[32];
  uint8_t slot;
} xhci_device_t;

typedef struct {
  usb_port_t usb;
  usb_speed_t speeds[16];
  uint32_t changes;
  uint8_t major, slot_type;
} xhci_port_t;

struct xhci_host {
  xhci_host_t *next;
  const pci_device_t *pci;
  pci_bar_t bar;
  volatile uint32_t *operational, *ports, *doorbells, *interrupter;
  xhci_dma_t dcbaa, erst, scratch_array, scratch_pages;
  xhci_ring_t commands, events;
  xhci_port_t *root_ports;
  xhci_device_t **slots;
  dma_addr_t address_limit;
  unsigned slot_count, port_count, context_size;
  pci_irq_t interrupt;
  xhci_completion_t pending;
  bool running, halted, failed;
};

static struct {
  xhci_host_t *hosts;
  mtask *worker;
} xhci;

_Static_assert(sizeof(xhci_trb_t) == 16, "xHCI TRB layout");
_Static_assert(__builtin_offsetof(xhci_device_t, usb) == 0,
               "USB device is the host device's base object");

static uint32_t xhci_read(volatile uint32_t *base, unsigned offset) {
  return base[offset / 4];
}

static void xhci_write(volatile uint32_t *base, unsigned offset,
                       uint32_t value) {
  base[offset / 4] = value;
}

static void xhci_write64(volatile uint32_t *base, unsigned offset,
                         uint64_t value) {
  xhci_write(base, offset, value);
  xhci_write(base, offset + 4, value >> 32);
}

static volatile uint32_t *xhci_map(xhci_host_t *host, uint64_t offset,
                                   unsigned size) {
  if (offset > host->bar.size || size > host->bar.size - offset ||
      offset > UINT64_MAX - host->bar.address) {
    return NULL;
  }
  return arch_mmio_map(host->bar.address + offset, size);
}

static bool xhci_wait_register(volatile uint32_t *base, unsigned offset,
                               uint32_t mask, uint32_t expected) {
  uint64_t deadline = monotonic_time_ns() + XHCI_TIMEOUT_MS * 1000000ull;
  do {
    uint32_t value = xhci_read(base, offset);
    if (value == UINT_MAX) {
      return false;
    }
    if ((value & mask) == expected) {
      return true;
    }
    sleep(1);
  } while (monotonic_time_ns() < deadline);
  usb_log("xhci: register wait failed offset=%x value=%08x "
          "mask=%08x expected=%08x\n",
          offset, xhci_read(base, offset), mask, expected);
  return false;
}

static bool xhci_dma_allocate(xhci_host_t *host, xhci_dma_t *memory,
                              unsigned size, unsigned alignment) {
  /* Pages already satisfy the controller's 64-byte alignment. Larger
   * alignment keeps a control data TRB within a single 64 KiB boundary. */
  if (alignment < XHCI_PAGE_SIZE) {
    alignment = XHCI_PAGE_SIZE;
  }
  unsigned allocation_size = size + alignment - XHCI_PAGE_SIZE;
  void *allocation = page_malloc(allocation_size);
  dma_addr_t address;
  if (allocation == NULL) {
    return false;
  }
  if (!dma_map(allocation, allocation_size, host->address_limit, &address)) {
    page_free(allocation, allocation_size);
    return false;
  }
  unsigned offset = (unsigned)(-address & (alignment - 1));
  *memory = (xhci_dma_t){allocation, (uint8_t *)allocation + offset,
                         address + offset, allocation_size};
  return true;
}

static void xhci_dma_release(xhci_host_t *host, xhci_dma_t *memory) {
  /* If a broken controller never acknowledges halt, quarantine its DMA
   * pages even after bus mastering is disabled. Never recycle live DMA. */
  if (memory->allocation != NULL && (host->running || host->halted)) {
    page_free(memory->allocation, memory->size);
    memset(memory, 0, sizeof(*memory));
  }
}

static bool xhci_ring_allocate(xhci_host_t *host, xhci_ring_t *ring) {
  ring->cycle = 1;
  return xhci_dma_allocate(host, &ring->memory, XHCI_PAGE_SIZE, XHCI_PAGE_SIZE);
}

static dma_addr_t xhci_enqueue(xhci_ring_t *ring, xhci_trb_t trb,
                               bool publish) {
  xhci_trb_t *entries = ring->memory.data;
  if (ring->index == XHCI_RING_SIZE - 1) {
    entries[ring->index].parameter = ring->memory.address;
    entries[ring->index].status = 0;
    dma_sync_for_device(&entries[ring->index], sizeof(*entries));
    *(volatile uint32_t *)&entries[ring->index].control =
        (XHCI_LINK << 10) | XHCI_TOGGLE_CYCLE | ring->cycle;
    dma_sync_for_device(&entries[ring->index], sizeof(*entries));
    ring->index = 0;
    ring->cycle ^= 1;
  }
  unsigned index = ring->index++;
  /* Publish ownership last, after the parameter and status are visible. */
  entries[index].parameter = trb.parameter;
  entries[index].status = trb.status;
  dma_sync_for_device(&entries[index], sizeof(*entries));
  *(volatile uint32_t *)&entries[index].control =
      trb.control | (ring->cycle ^ !publish);
  dma_sync_for_device(&entries[index], sizeof(*entries));
  return ring->memory.address + index * sizeof(*entries);
}

static void xhci_stop(xhci_host_t *host) {
  host->running = false;
  xhci_write(host->interrupter, XHCI_IMAN, 1);
  xhci_write(host->operational, XHCI_COMMAND, 0);
  host->halted = xhci_wait_register(host->operational, XHCI_STATUS, XHCI_HALTED,
                                    XHCI_HALTED);
  pci_command_update(host->pci, 0, PCI_COMMAND_BUS_MASTER);
  pci_irq_release(&host->interrupt);
  if (!host->halted) {
    usb_log("xhci: halt timeout; DMA memory quarantined\n");
  }
}

static void xhci_interrupt_arm(xhci_device_t *device, unsigned dci);

static bool xhci_interrupt(unsigned irq) {
  bool reschedule = false;
  for (xhci_host_t *host = xhci.hosts; host != NULL; host = host->next) {
    if (!host->running || host->interrupt.irq != irq) {
      continue;
    }
    bool wake_worker = false;
    uint32_t status = xhci_read(host->operational, XHCI_STATUS);
    if (!(status &
          (XHCI_EVENT_PENDING | XHCI_HOST_ERROR | XHCI_CONTROLLER_ERROR))) {
      continue;
    }
    xhci_write(host->operational, XHCI_STATUS,
               status &
                   (XHCI_EVENT_PENDING | XHCI_PORT_PENDING | XHCI_HOST_ERROR));
    xhci_write(host->interrupter, XHCI_IMAN, 3);
    if (status & (XHCI_HOST_ERROR | XHCI_CONTROLLER_ERROR)) {
      host->failed = true;
      host->pending.code = UINT_MAX;
      wake_worker = true;
    }
    volatile xhci_trb_t *events = host->events.memory.data;
    for (unsigned count = 0; count < XHCI_RING_SIZE; count++) {
      volatile xhci_trb_t *entry = &events[host->events.index];
      if ((entry->control & XHCI_CYCLE) != host->events.cycle) {
        break;
      }
      dma_sync_for_cpu((const void *)entry, sizeof(*entry));
      xhci_trb_t event = *entry;
      unsigned type = (event.control >> 10) & 63;
      unsigned code = event.status >> 24;
      unsigned slot = event.control >> 24;
      if (type == XHCI_PORT_EVENT) {
        wake_worker = true;
        unsigned port = (event.parameter >> 24) & 255;
        if (port != 0 && port <= host->port_count) {
          xhci_port_t *root = &host->root_ports[port - 1];
          uint32_t port_status = xhci_read(host->ports, (port - 1) * 16);
          root->changes |= port_status & XHCI_PORT_CHANGES;
          root->usb.dirty = true;
          if (root->usb.device != NULL &&
              (!(port_status & XHCI_PORT_CONNECTED) ||
               (port_status & XHCI_PORT_CONNECT_CHANGE))) {
            root->usb.device->connected = false;
          }
          xhci_write(host->ports, (port - 1) * 16,
                     port_status & (XHCI_PORT_WRITABLE | XHCI_PORT_CHANGES));
        }
      } else if (type == XHCI_COMMAND_EVENT && host->pending.active &&
                 event.parameter == host->pending.address) {
        wake_worker = true;
        host->pending.slot = slot;
        host->pending.code = code;
      } else if (type == XHCI_TRANSFER_EVENT && slot > 0 &&
                 slot <= host->slot_count && host->slots[slot] != NULL) {
        xhci_device_t *device = host->slots[slot];
        unsigned dci = (event.control >> 16) & 31;
        xhci_endpoint_t *ep = device->endpoints[dci];
        if (ep != NULL && ep->pending.active) {
          wake_worker |= ep->callback == NULL;
          if (code == XHCI_SHORT_PACKET) {
            ep->pending.residual = event.status & 0xffffff;
          }
          if (dci != 1 || code != XHCI_SHORT_PACKET) {
            ep->pending.code = code;
          }
          if (ep->callback != NULL && usb_connected(&device->usb)) {
            ep->pending.active = false;
            if ((code == XHCI_SUCCESS || code == XHCI_SHORT_PACKET) &&
                ep->pending.residual <= ep->capacity) {
              dma_sync_for_cpu(ep->buffer.data, ep->capacity);
              reschedule |= ep->callback(ep->context, ep->buffer.data,
                                         ep->capacity - ep->pending.residual);
              xhci_interrupt_arm(device, dci);
            } else {
              device->usb.connected = false;
              wake_worker = true;
              device->usb.port->dirty = true;
            }
          }
        }
      } else if (type == XHCI_HOST_EVENT && code != XHCI_SUCCESS) {
        host->failed = true;
        host->pending.code = UINT_MAX;
        wake_worker = true;
      }
      if (++host->events.index == XHCI_RING_SIZE) {
        host->events.index = 0;
        host->events.cycle ^= 1;
      }
    }
    if (host->failed) {
      xhci_write(host->interrupter, XHCI_IMAN, 1);
    }
    xhci_write64(host->interrupter, XHCI_EVENT_DEQUEUE,
                 (host->events.memory.address +
                  host->events.index * sizeof(xhci_trb_t)) |
                     8);
    if (wake_worker && xhci.worker != NULL && xhci.worker->state == WAITING &&
        xhci.worker->wait_reason == WAIT_REASON_USB) {
      task_run(xhci.worker);
      reschedule = true;
    }
  }
  return reschedule;
}

static int xhci_wait_completion(xhci_host_t *host, xhci_completion_t *pending,
                                usb_device_t *device, unsigned timeout_ms) {
  uint64_t deadline = monotonic_time_ns() + timeout_ms * 1000000ull;
  irq_state_t state = irq_save();
  struct TIMER *timer = timer_alloc();
  if (timer != NULL) {
    unsigned char byte;
    struct FIFO8 fifo;
    fifo8_init(&fifo, 1, &byte);
    timer_init(timer, &fifo, 1);
    timer->waiter = current_task();
    timer_settime(timer, timeout_ms / 10 + 1);
    while (pending->code == 0 && !host->failed &&
           (device == NULL || usb_connected(device)) &&
           fifo8_status(&fifo) == 0 && monotonic_time_ns() < deadline) {
      task_fall_blocked_reason(WAITING, WAIT_REASON_USB);
    }
    timer_free(timer);
  }
  pending->active = false;
  irq_restore(state);
  if (device != NULL && !usb_connected(device)) {
    return USB_ERROR_DISCONNECTED;
  }
  if (host->failed || pending->code == 0 || pending->code == UINT_MAX) {
    volatile xhci_trb_t *event =
        &((volatile xhci_trb_t *)host->events.memory.data)[host->events.index];
    dma_sync_for_cpu((const void *)event, sizeof(*event));
    usb_log("xhci: %04x:%02x:%02x.%u timeout/failure irq=%u code=%u\n",
            host->pci->segment, host->pci->bus, host->pci->slot,
            host->pci->function, host->interrupt.irq, pending->code);
    usb_log("xhci: cmd=%08x sts=%08x iman=%08x event=%08x cycle=%u\n",
            xhci_read(host->operational, XHCI_COMMAND),
            xhci_read(host->operational, XHCI_STATUS),
            xhci_read(host->interrupter, XHCI_IMAN), event->control,
            host->events.cycle);
    xhci_stop(host);
    return USB_ERROR_IO;
  }
  if (pending->code == 6) {
    return USB_ERROR_STALL;
  }
  return pending->code == XHCI_SUCCESS || pending->code == XHCI_SHORT_PACKET
             ? 0
             : USB_ERROR_IO;
}

static bool xhci_command(xhci_host_t *host, unsigned type, uint64_t parameter,
                         uint32_t flags) {
  if (!host->running || host->failed) {
    return false;
  }
  irq_state_t state = irq_save();
  memset(&host->pending, 0, sizeof(host->pending));
  host->pending.active = true;
  host->pending.address = xhci_enqueue(
      &host->commands,
      (xhci_trb_t){.parameter = parameter, .control = (type << 10) | flags},
      true);
  xhci_write(host->doorbells, 0, 0);
  irq_restore(state);
  int result =
      xhci_wait_completion(host, &host->pending, NULL, XHCI_TIMEOUT_MS);
  if (result != 0) {
    usb_log("xhci: command type=%u slot=%u code=%u failed\n", type, flags >> 24,
            host->pending.code);
  }
  return result == 0;
}

static bool xhci_endpoint_reset(xhci_device_t *device, unsigned dci) {
  xhci_endpoint_t *ep = device->endpoints[dci];
  uint32_t flags = (uint32_t)device->slot << 24 | dci << 16;
  dma_addr_t dequeue =
      ep->ring.memory.address + ep->ring.index * sizeof(xhci_trb_t);
  uint32_t *output = (uint32_t *)((uint8_t *)device->output.data +
                                  dci * device->host->context_size);
  dma_sync_for_cpu(output, device->host->context_size);
  unsigned state = output[0] & 7;
  if (state == 2) {
    if (!xhci_command(device->host, XHCI_RESET_ENDPOINT, 0, flags))
      return false;
  } else if (state == 1) {
    if (!xhci_command(device->host, XHCI_STOP_ENDPOINT, 0, flags))
      return false;
  } else if (state != 3) {
    return false;
  }
  return xhci_command(device->host, XHCI_SET_DEQUEUE, dequeue | ep->ring.cycle,
                      flags);
}

static int xhci_control(usb_device_t *usb, const usb_setup_t *setup,
                        void *data) {
  xhci_device_t *device = (xhci_device_t *)usb;
  xhci_host_t *host = device->host;
  xhci_endpoint_t *ep = device->endpoints[1];
  if (!host->running || host->failed || !usb_connected(usb) ||
      (setup->length != 0 && data == NULL)) {
    return USB_ERROR_DISCONNECTED;
  }
  if (setup->length > ep->capacity) {
    xhci_dma_t buffer = {0};
    if (!xhci_dma_allocate(host, &buffer, setup->length, 65536)) {
      return USB_ERROR_IO;
    }
    xhci_dma_release(host, &ep->buffer);
    ep->buffer = buffer;
    ep->capacity = setup->length;
  }
  bool input = (setup->type & USB_DIRECTION_IN) != 0;
  if (setup->length != 0 && !input) {
    memcpy(ep->buffer.data, data, setup->length);
    dma_sync_for_device(ep->buffer.data, setup->length);
  }
  uint64_t packet;
  memcpy(&packet, setup, sizeof(packet));
  irq_state_t state = irq_save();
  ep->pending = (xhci_completion_t){.active = true};
  dma_addr_t first = xhci_enqueue(
      &ep->ring,
      (xhci_trb_t){.parameter = packet,
                   .status = sizeof(packet),
                   .control =
                       (XHCI_SETUP << 10) | XHCI_IMMEDIATE_DATA |
                       (setup->length != 0 ? (input ? 3u : 2u) << 16 : 0)},
      false);
  if (setup->length != 0) {
    xhci_enqueue(&ep->ring,
                 (xhci_trb_t){.parameter = ep->buffer.address,
                              .status = setup->length,
                              .control = (XHCI_DATA << 10) |
                                         XHCI_SHORT_INTERRUPT |
                                         (input ? XHCI_DIRECTION_IN : 0)},
                 true);
  }
  ep->pending.address = xhci_enqueue(
      &ep->ring,
      (xhci_trb_t){.control =
                       (XHCI_STATUS_STAGE << 10) | XHCI_COMPLETION_INTERRUPT |
                       (setup->length == 0 || !input ? XHCI_DIRECTION_IN : 0)},
      true);
  xhci_trb_t *setup_trb =
      (xhci_trb_t *)((uint8_t *)ep->ring.memory.data +
                     (size_t)(first - ep->ring.memory.address));
  setup_trb->control ^= XHCI_CYCLE;
  dma_sync_for_device(setup_trb, sizeof(*setup_trb));
  xhci_write(host->doorbells, device->slot * 4, 1);
  irq_restore(state);
  int result = xhci_wait_completion(host, &ep->pending, usb, XHCI_TIMEOUT_MS);
  if (result == USB_ERROR_STALL && !xhci_endpoint_reset(device, 1)) {
    xhci_stop(host);
  }
  if (result < 0) {
    return result;
  }
  if (ep->pending.residual > setup->length) {
    return USB_ERROR_IO;
  }
  unsigned actual = setup->length - ep->pending.residual;
  if (actual != 0 && input) {
    dma_sync_for_cpu(ep->buffer.data, actual);
    memcpy(data, ep->buffer.data, actual);
  }
  return actual;
}

static bool xhci_packet_size(usb_device_t *usb, uint16_t size) {
  xhci_device_t *device = (xhci_device_t *)usb;
  xhci_host_t *host = device->host;
  uint32_t *input = device->input.data;
  uint32_t *ep = (uint32_t *)((uint8_t *)input + 2 * host->context_size);
  if ((ep[1] >> 16) == size) {
    return true;
  }
  input[1] = 2;
  dma_sync_for_cpu(device->output.data, 32 * host->context_size);
  memcpy(ep, (uint8_t *)device->output.data + host->context_size,
         host->context_size);
  ep[0] &= ~7u; /* Endpoint State is output-only. */
  ep[1] = (ep[1] & 0xffff) | (uint32_t)size << 16;
  dma_sync_for_device(input, 33 * host->context_size);
  return xhci_command(host, XHCI_EVALUATE_CONTEXT, device->input.address,
                      (uint32_t)device->slot << 24);
}

static xhci_endpoint_t *xhci_endpoint_allocate(xhci_host_t *host,
                                               unsigned capacity) {
  xhci_endpoint_t *ep = malloc(sizeof(*ep));
  if (ep == NULL) {
    return NULL;
  }
  memset(ep, 0, sizeof(*ep));
  if (!xhci_ring_allocate(host, &ep->ring) ||
      !xhci_dma_allocate(host, &ep->buffer, capacity,
                         capacity > XHCI_PAGE_SIZE ? 65536 : XHCI_PAGE_SIZE)) {
    xhci_dma_release(host, &ep->ring.memory);
    xhci_dma_release(host, &ep->buffer);
    free(ep);
    return NULL;
  }
  ep->capacity = capacity;
  return ep;
}

static bool xhci_configure(usb_device_t *usb, const usb_endpoint_t *endpoints,
                           unsigned count) {
  xhci_device_t *device = (xhci_device_t *)usb;
  xhci_host_t *host = device->host;
  uint32_t *input = device->input.data;
  memset(input, 0, 33 * host->context_size);
  dma_sync_for_cpu(device->output.data, 32 * host->context_size);
  uint32_t *slot = (uint32_t *)((uint8_t *)input + host->context_size);
  memcpy(slot, device->output.data, host->context_size);
  slot[3] = 0; /* Device address/state are reserved in an input slot context. */
  input[1] = 1;
  unsigned last = 1;
  for (unsigned i = 0; i < count; i++) {
    const usb_endpoint_t *descriptor = &endpoints[i];
    unsigned dci = (descriptor->address & 15) * 2 +
                   ((descriptor->address & USB_DIRECTION_IN) != 0);
    unsigned type = descriptor->type;
    unsigned packet = descriptor->packet_size;
    if (dci < 2 || dci > 31 || (descriptor->address & 0x70) != 0 ||
        device->endpoints[dci] != NULL || packet == 0 || packet > 1024 ||
        descriptor->burst > 15 || descriptor->mult > 2 ||
        (usb->speed == USB_SPEED_HIGH && descriptor->burst > 2) ||
        (type != USB_ENDPOINT_BULK && type != USB_ENDPOINT_INTERRUPT)) {
      return false;
    }
    unsigned interval = 0;
    unsigned payload = 0;
    if (type == USB_ENDPOINT_INTERRUPT) {
      if (descriptor->interval == 0) {
        return false;
      }
      if (usb->speed == USB_SPEED_LOW || usb->speed == USB_SPEED_FULL) {
        unsigned milliseconds = descriptor->interval;
        while (milliseconds > 1) {
          milliseconds >>= 1;
          interval++;
        }
        interval += 3;
      } else {
        if (descriptor->interval > 16) {
          return false;
        }
        interval = descriptor->interval - 1;
      }
      payload = packet * (descriptor->burst + 1) * (descriptor->mult + 1);
      if (usb->speed == USB_SPEED_SUPER) {
        if (descriptor->bytes_per_interval == 0 ||
            descriptor->bytes_per_interval > payload) {
          return false;
        }
        payload = descriptor->bytes_per_interval;
      }
    }
    xhci_endpoint_t *ep = xhci_endpoint_allocate(
        host, type == USB_ENDPOINT_BULK ? 65536 : payload);
    if (ep == NULL) {
      return false;
    }
    device->endpoints[dci] = ep;
    input[1] |= 1u << dci;
    if (dci > last) {
      last = dci;
    }
    uint32_t *context =
        (uint32_t *)((uint8_t *)input + (dci + 1) * host->context_size);
    unsigned ep_type =
        type | ((descriptor->address & USB_DIRECTION_IN) ? 4 : 0);
    context[0] = interval << 16 |
                 (type == USB_ENDPOINT_INTERRUPT ? descriptor->mult << 8 : 0);
    context[1] = packet << 16 | descriptor->burst << 8 | ep_type << 3 | 3u << 1;
    context[2] = (uint32_t)ep->ring.memory.address | 1;
    context[3] = ep->ring.memory.address >> 32;
    context[4] = (payload ? payload : packet) | payload << 16;
  }
  slot[0] = (slot[0] & ~(31u << 27)) | last << 27;
  dma_sync_for_device(input, 33 * host->context_size);
  return xhci_command(host, XHCI_CONFIGURE_ENDPOINT, device->input.address,
                      (uint32_t)device->slot << 24);
}

static int xhci_transfer(usb_device_t *usb, uint8_t address, void *data,
                         unsigned length) {
  xhci_device_t *device = (xhci_device_t *)usb;
  unsigned dci = (address & 15) * 2 + ((address & USB_DIRECTION_IN) != 0);
  xhci_endpoint_t *ep = device->endpoints[dci];
  if (!usb_connected(usb) || !device->host->running || device->host->failed) {
    return USB_ERROR_DISCONNECTED;
  }
  if (dci < 2 || ep == NULL || ep->callback != NULL || ep->pending.active ||
      length > ep->capacity || (length != 0 && data == NULL)) {
    return USB_ERROR_IO;
  }
  bool input = (address & USB_DIRECTION_IN) != 0;
  if (!input && length != 0) {
    memcpy(ep->buffer.data, data, length);
    dma_sync_for_device(ep->buffer.data, length);
  }
  irq_state_t state = irq_save();
  ep->pending = (xhci_completion_t){.active = true};
  ep->pending.address = xhci_enqueue(
      &ep->ring,
      (xhci_trb_t){.parameter = ep->buffer.address,
                   .status = length,
                   .control = (XHCI_NORMAL << 10) | XHCI_COMPLETION_INTERRUPT |
                              XHCI_SHORT_INTERRUPT},
      true);
  xhci_write(device->host->doorbells, device->slot * 4, dci);
  irq_restore(state);
  int result = xhci_wait_completion(device->host, &ep->pending, usb,
                                    XHCI_TRANSFER_TIMEOUT_MS);
  if (result < 0) {
    return result;
  }
  if (ep->pending.residual > length) {
    return USB_ERROR_IO;
  }
  unsigned actual = length - ep->pending.residual;
  if (input && actual != 0) {
    dma_sync_for_cpu(ep->buffer.data, actual);
    memcpy(data, ep->buffer.data, actual);
  }
  return actual;
}

static void xhci_interrupt_arm(xhci_device_t *device, unsigned dci) {
  xhci_endpoint_t *ep = device->endpoints[dci];
  ep->pending = (xhci_completion_t){.active = true};
  ep->pending.address = xhci_enqueue(
      &ep->ring,
      (xhci_trb_t){.parameter = ep->buffer.address,
                   .status = ep->capacity,
                   .control = (XHCI_NORMAL << 10) | XHCI_COMPLETION_INTERRUPT |
                              XHCI_SHORT_INTERRUPT},
      true);
  xhci_write(device->host->doorbells, device->slot * 4, dci);
}

static bool xhci_interrupt_start(usb_device_t *usb, uint8_t address,
                                 usb_interrupt_callback_t callback,
                                 void *context) {
  xhci_device_t *device = (xhci_device_t *)usb;
  unsigned dci = (address & 15) * 2 + ((address & USB_DIRECTION_IN) != 0);
  xhci_endpoint_t *ep = device->endpoints[dci];
  if (ep == NULL || dci < 2 || !(address & USB_DIRECTION_IN) ||
      ep->pending.active || callback == NULL || !usb_connected(usb)) {
    return false;
  }
  irq_state_t state = irq_save();
  ep->callback = callback;
  ep->context = context;
  xhci_interrupt_arm(device, dci);
  irq_restore(state);
  return true;
}

static bool xhci_clear_halt(usb_device_t *usb, uint8_t address) {
  xhci_device_t *device = (xhci_device_t *)usb;
  unsigned dci = (address & 15) * 2 + ((address & USB_DIRECTION_IN) != 0);
  if (dci < 2 || device->endpoints[dci] == NULL) {
    return false;
  }
  usb_setup_t setup = {.type = 2, .request = 1, .index = address};
  return xhci_control(usb, &setup, NULL) == 0 &&
         xhci_endpoint_reset(device, dci);
}

static const usb_host_ops_t xhci_usb_ops;

static void xhci_device_release(usb_device_t *usb) {
  xhci_device_t *device = (xhci_device_t *)usb;
  xhci_host_t *host = device->host;
  /* Quiesce callbacks, then remove descendants before their hub's slot. */
  usb_disconnect(usb);
  if (device->slot != 0 && host->running &&
      !xhci_command(host, XHCI_DISABLE_SLOT, 0, (uint32_t)device->slot << 24)) {
    xhci_stop(host);
  }
  if (device->slot != 0) {
    uint64_t *dcbaa = host->dcbaa.data;
    dcbaa[device->slot] = 0;
    dma_sync_for_device(&dcbaa[device->slot], sizeof(*dcbaa));
  }
  xhci_dma_release(host, &device->input);
  xhci_dma_release(host, &device->output);
  if (device->slot != 0) {
    host->slots[device->slot] = NULL;
  }
  for (unsigned dci = 1; dci < 32; dci++) {
    xhci_endpoint_t *ep = device->endpoints[dci];
    if (ep != NULL) {
      xhci_dma_release(host, &ep->ring.memory);
      xhci_dma_release(host, &ep->buffer);
      free(ep);
    }
  }
  if (usb->port != NULL && usb->port->device == usb) {
    usb->port->device = NULL;
  }
  free(usb->configuration);
  free(device);
}

static bool xhci_device_create(xhci_host_t *host, usb_port_t *port,
                               unsigned speed_id) {
  unsigned route = 0, depth = 0, tt = 0, mtt = 0;
  usb_port_t *upstream = port;
  while (upstream->hub != NULL) {
    if (depth++ == 5) { /* xHCI's route string encodes five downstream tiers. */
      usb_log("usb: hub topology exceeds five downstream tiers\n");
      return false;
    }
    route = (route << 4) | (upstream->number < 15 ? upstream->number : 15);
    upstream = upstream->hub->port;
  }
  unsigned root = upstream->number;
  xhci_port_t *root_port = &host->root_ports[root - 1];
  usb_speed_t speed = root_port->speeds[speed_id];
  if (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL) {
    for (upstream = port; upstream->hub != NULL;
         upstream = upstream->hub->port) {
      xhci_device_t *parent = (xhci_device_t *)upstream->hub;
      if (parent->usb.speed == USB_SPEED_HIGH) {
        dma_sync_for_cpu(parent->output.data, host->context_size);
        tt = parent->slot | (unsigned)upstream->number << 8;
        mtt = ((uint32_t *)parent->output.data)[0] & (1u << 25);
        break;
      }
    }
  }
  xhci_device_t *device = malloc(sizeof(*device));
  if (device == NULL) {
    return false;
  }
  memset(device, 0, sizeof(*device));
  device->host = host;
  device->usb.ops = &xhci_usb_ops;
  device->usb.port = port;
  device->usb.speed = speed;
  device->endpoints[1] = xhci_endpoint_allocate(host, XHCI_PAGE_SIZE);
  device->usb.connected = true;
  port->device = &device->usb;
  if (device->endpoints[1] == NULL ||
      !xhci_dma_allocate(host, &device->input, 33 * host->context_size, 64) ||
      !xhci_dma_allocate(host, &device->output, 32 * host->context_size, 64) ||
      !xhci_command(host, XHCI_ENABLE_SLOT, 0,
                    (uint32_t)root_port->slot_type << 16)) {
    xhci_device_release(&device->usb);
    return false;
  }
  unsigned slot = host->pending.slot;
  if (slot == 0 || slot > host->slot_count) {
    xhci_stop(host);
    xhci_device_release(&device->usb);
    return false;
  }
  device->slot = slot;
  host->slots[slot] = device;
  uint64_t *dcbaa = host->dcbaa.data;
  dcbaa[slot] = device->output.address;
  dma_sync_for_device(&dcbaa[slot], sizeof(*dcbaa));
  uint32_t *input = device->input.data;
  uint32_t *context = (uint32_t *)((uint8_t *)input + host->context_size);
  uint32_t *endpoint = (uint32_t *)((uint8_t *)context + host->context_size);
  unsigned packet_size = speed == USB_SPEED_SUPER  ? 512
                         : speed == USB_SPEED_HIGH ? 64
                                                   : 8;
  input[1] = 3; /* Add Slot and Endpoint 0 contexts. */
  context[0] = 1u << 27 | mtt | speed_id << 20 | route;
  context[1] = root << 16;
  context[2] = tt;
  endpoint[1] = packet_size << 16 | 4u << 3 | 3u << 1;
  endpoint[2] = (uint32_t)device->endpoints[1]->ring.memory.address | 1;
  endpoint[3] = device->endpoints[1]->ring.memory.address >> 32;
  endpoint[4] = 8; /* Average TRB length for the control endpoint. */
  dma_sync_for_device(input, 33 * host->context_size);
  if (!xhci_command(host, XHCI_ADDRESS_DEVICE, device->input.address,
                    slot << 24)) {
    usb_log("xhci: port %u address device failed\n", root);
    xhci_device_release(&device->usb);
    return false;
  }
  if (!usb_enumerate(&device->usb)) {
    usb_log("xhci: port %u descriptor enumeration failed\n", root);
    xhci_device_release(&device->usb);
    return false;
  }
  if (!usb_bind(&device->usb)) {
    usb_log("usb: port %u configuration failed\n", root);
    xhci_device_release(&device->usb);
    return false;
  }
  static const char *const speeds[] = {"unknown", "low", "full", "high",
                                       "super"};
  usb_device_descriptor_t *descriptor = &device->usb.descriptor;
  usb_log("usb: %04x:%02x:%02x.%u port=%u slot=%u speed=%s id=%04x:%04x "
          "usb=%04x class=%02x interfaces=%u route=%05x tt=%04x enumerated\n",
          host->pci->segment, host->pci->bus, host->pci->slot,
          host->pci->function, root, slot, speeds[speed], descriptor->vendor,
          descriptor->product, descriptor->usb_version,
          descriptor->device_class, device->usb.configuration[4], route, tt);
  return true;
}
static bool xhci_hub_configure(usb_device_t *usb, unsigned ports,
                               unsigned think_time, bool multi_tt) {
  xhci_device_t *device = (xhci_device_t *)usb;
  xhci_host_t *host = device->host;
  uint32_t *input = device->input.data;
  uint32_t *slot = (uint32_t *)((uint8_t *)input + host->context_size);
  memset(input, 0, 33 * host->context_size);
  dma_sync_for_cpu(device->output.data, host->context_size);
  memcpy(slot, device->output.data, host->context_size);
  input[1] = 1; /* Configure hub fields without changing endpoint contexts. */
  slot[3] = 0;
  slot[0] |= 1u << 26;
  slot[1] = (slot[1] & 0x00ffffffu) | ports << 24;
  if (usb->speed == USB_SPEED_HIGH) {
    slot[0] = (slot[0] & ~(1u << 25)) | (multi_tt ? 1u << 25 : 0);
    slot[2] = (slot[2] & ~(3u << 16)) | think_time << 16;
  }
  dma_sync_for_device(input, 33 * host->context_size);
  return xhci_command(host, XHCI_CONFIGURE_ENDPOINT, device->input.address,
                      (uint32_t)device->slot << 24);
}

static bool xhci_hub_attach(usb_port_t *port, usb_speed_t speed) {
  xhci_host_t *host = ((xhci_device_t *)port->hub)->host;
  usb_port_t *root = port;
  while (root->hub != NULL) {
    root = root->hub->port;
  }
  for (unsigned id = 1; id < 16; id++) {
    if (host->root_ports[root->number - 1].speeds[id] == speed) {
      return xhci_device_create(host, port, id);
    }
  }
  usb_log("usb: hub port %u has no host speed mapping for %u\n", port->number,
          speed);
  return false;
}

static const usb_host_ops_t xhci_usb_ops = {
    .control = xhci_control,
    .set_packet_size = xhci_packet_size,
    .configure = xhci_configure,
    .transfer = xhci_transfer,
    .interrupt = xhci_interrupt_start,
    .clear_halt = xhci_clear_halt,
    .hub = xhci_hub_configure,
    .attach = xhci_hub_attach,
    .detach = xhci_device_release,
};

static void xhci_attach(xhci_host_t *host, unsigned index) {
  xhci_port_t *port = &host->root_ports[index];
  usb_log("xhci: %04x:%02x:%02x.%u port=%u USB%u PORTSC=%08x connected\n",
          host->pci->segment, host->pci->bus, host->pci->slot,
          host->pci->function, index + 1, port->major,
          xhci_read(host->ports, index * 16));
  if (port->major == 0) {
    usb_log("xhci: port %u has no supported USB protocol\n", index + 1);
    return;
  }
  /* Require 100 ms of continuous connection, with a bounded settling time. */
  uint64_t deadline = monotonic_time_ns() + XHCI_TIMEOUT_MS * 1000000ull;
  uint32_t status;
  for (;;) {
    irq_state_t state = irq_save();
    port->changes &= ~XHCI_PORT_CONNECT_CHANGE;
    status = xhci_read(host->ports, index * 16);
    xhci_write(host->ports, index * 16,
               status & (XHCI_PORT_WRITABLE | XHCI_PORT_CONNECT_CHANGE));
    irq_restore(state);
    sleep(100);
    state = irq_save();
    status = xhci_read(host->ports, index * 16);
    bool changed = ((port->changes | status) & XHCI_PORT_CONNECT_CHANGE) != 0;
    irq_restore(state);
    if (!(status & XHCI_PORT_CONNECTED)) {
      return;
    }
    if (!changed) {
      break;
    }
    if (monotonic_time_ns() >= deadline) {
      usb_log("xhci: port %u debounce timeout\n", index + 1);
      return;
    }
  }
  xhci_write(host->ports, index * 16,
             (status & XHCI_PORT_WRITABLE) |
                 (port->major >= 3 ? XHCI_PORT_WARM_RESET : XHCI_PORT_RESET));
  if (!xhci_wait_register(host->ports, index * 16,
                          XHCI_PORT_RESET | XHCI_PORT_WARM_RESET |
                              XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED,
                          XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED)) {
    usb_log("xhci: port %u reset failed\n", index + 1);
    return;
  }
  sleep(10);
  status = xhci_read(host->ports, index * 16);
  unsigned speed_id = (status >> 10) & 15;
  usb_speed_t speed = port->speeds[speed_id];
  if (speed == USB_SPEED_UNKNOWN) {
    usb_log("xhci: port %u unsupported speed ID %u\n", index + 1, speed_id);
    return;
  }
  xhci_device_create(host, &port->usb, speed_id);
}

static void xhci_service(void) {
  irq_enable();
  for (xhci_host_t *host = xhci.hosts; host != NULL; host = host->next) {
    if (!xhci_command(host, XHCI_NOOP, 0, 0)) {
      xhci_stop(host);
    } else {
      usb_log("xhci: %04x:%02x:%02x.%u IRQ %u command self-test passed\n",
              host->pci->segment, host->pci->bus, host->pci->slot,
              host->pci->function, host->interrupt.irq);
    }
  }
  for (;;) {
    bool work = false;
    for (xhci_host_t *host = xhci.hosts; host != NULL; host = host->next) {
      if (host->failed && host->running) {
        xhci_stop(host);
      }
      for (unsigned index = 0; index < host->port_count; index++) {
        irq_state_t state = irq_save();
        xhci_port_t *port = &host->root_ports[index];
        bool dirty =
            port->usb.dirty || (!host->running && port->usb.device != NULL);
        uint32_t changes = port->changes;
        port->usb.dirty = false;
        port->changes = 0;
        irq_restore(state);
        if (!dirty) {
          continue;
        }
        work = true;
        uint32_t port_status = xhci_read(host->ports, index * 16);
        bool connected =
            host->running && (port_status & XHCI_PORT_CONNECTED) != 0;
#ifdef KERNEL_USB_DEBUG
        usb_log("xhci: %04x:%02x:%02x.%u port=%u USB%u PORTSC=%08x "
                "connected=%u power=%u link=%u\n",
                host->pci->segment, host->pci->bus, host->pci->slot,
                host->pci->function, index + 1, port->major, port_status,
                connected, (port_status >> 9) & 1, (port_status >> 5) & 15);
#endif
        if (!connected || (changes & XHCI_PORT_CONNECT_CHANGE)) {
          port->usb.attempted = false;
        }
        if (port->usb.device != NULL &&
            (!connected || !port->usb.device->connected ||
             (changes & XHCI_PORT_CONNECT_CHANGE))) {
          usb_log("usb: %04x:%02x:%02x.%u port=%u disconnected\n",
                  host->pci->segment, host->pci->bus, host->pci->slot,
                  host->pci->function, index + 1);
          xhci_device_release(port->usb.device);
        }
        if (host->running && connected && !port->usb.attempted) {
          port->usb.attempted = true;
          xhci_attach(host, index);
        }
      }
    }
    work |= usb_service_work();
    irq_state_t state = irq_save();
    work |= usb_work_pending();
    /* Recheck after taking the interrupt lock: a port can change between
     * the scan above and publishing the wait. */
    for (xhci_host_t *host = xhci.hosts; !work && host != NULL;
         host = host->next) {
      for (unsigned index = 0; host->running && index < host->port_count;
           index++) {
        work |= host->failed || host->root_ports[index].usb.dirty;
      }
    }
    if (!work) {
      task_fall_blocked_reason(WAITING, WAIT_REASON_USB);
    }
    irq_restore(state);
  }
}

static bool xhci_capabilities(xhci_host_t *host, unsigned offset) {
  while (offset != 0) {
    volatile uint32_t *cap = xhci_map(host, offset, 16);
    if (cap == NULL) {
      return false;
    }
    uint32_t header = xhci_read(cap, 0);
    unsigned type = header & 255;
    unsigned next = (header >> 8) & 255;
    if (type == 1) {
      usb_log("xhci: legacy ownership BIOS=%u OS=%u\n", (header >> 16) & 1,
              (header >> 24) & 1);
      xhci_write(cap, 0, header | (1u << 24));
      if (!xhci_wait_register(cap, 0, 1u << 16, 0)) {
        usb_log("xhci: firmware ownership timeout\n");
        return false;
      }
      /* Preserve reserved bits, disable SMI enables, clear SMI status. */
      xhci_write(cap, 4, (xhci_read(cap, 4) & 0x000e1feeu) | 0xe0000000u);
    } else if (type == 2 && xhci_read(cap, 4) == 0x20425355) {
      unsigned major = header >> 24;
      uint32_t ports = xhci_read(cap, 8);
      unsigned first = ports & 255, count = (ports >> 8) & 255;
      unsigned speed_count = ports >> 28;
      unsigned slot_type = xhci_read(cap, 12) & 31;
      usb_log("xhci: protocol USB%u.%x first=%u count=%u speed-ids=%u\n", major,
              (header >> 16) & 255, first, count, speed_count);
      if (count != 0 && (first == 0 || first > host->port_count ||
                         count > host->port_count - first + 1)) {
        return false;
      }
      cap = xhci_map(host, offset, 16 + speed_count * 4);
      if (cap == NULL) {
        return false;
      }
      usb_speed_t speeds[16] = {0};
      if (speed_count == 0) {
        if (major == 2) {
          speeds[1] = USB_SPEED_FULL;
          speeds[2] = USB_SPEED_LOW;
          speeds[3] = USB_SPEED_HIGH;
        } else if (major == 3) {
          speeds[4] = USB_SPEED_SUPER;
        }
      }
      for (unsigned i = 0; i < speed_count; i++) {
        uint32_t entry = xhci_read(cap, 16 + i * 4);
        uint64_t rate = entry >> 16;
        for (unsigned exponent = (entry >> 4) & 3; exponent != 0; exponent--) {
          rate *= 1000;
        }
        unsigned id = entry & 15;
        if (id == 0) {
          return false;
        }
        speeds[id] = major == 3          ? USB_SPEED_SUPER
                     : rate == 1500000   ? USB_SPEED_LOW
                     : rate == 12000000  ? USB_SPEED_FULL
                     : rate == 480000000 ? USB_SPEED_HIGH
                                         : USB_SPEED_UNKNOWN;
      }
      for (unsigned i = first - 1; i < first - 1 + count; i++) {
        xhci_port_t *port = &host->root_ports[i];
        if (port->major != 0) {
          return false;
        }
        port->major = major == 2 || major == 3 ? major : 0;
        port->slot_type = slot_type;
        memcpy(port->speeds, speeds, sizeof(speeds));
      }
    }
    if (next == 0) {
      break;
    }
    if (offset > UINT_MAX - next * 4) {
      return false;
    }
    offset += next * 4;
  }
  return true;
}

static const char *xhci_host_initialize(xhci_host_t *host) {
  if (!pci_read_bar(host->pci, 0, &host->bar) || host->bar.type == PCI_BAR_IO) {
    return "BAR0 aperture";
  }
  pci_command_update(host->pci, PCI_COMMAND_MEMORY, 0);
  volatile uint32_t *cap = xhci_map(host, 0, 32);
  if (cap == NULL) {
    return "capability mapping";
  }
  uint32_t version = xhci_read(cap, 0);
  uint32_t hcs1 = xhci_read(cap, 4), hcs2 = xhci_read(cap, 8);
  uint32_t hcc = xhci_read(cap, 16);
  usb_log("xhci: BAR=%llx size=%llx ver=%04x HCS=%08x/%08x HCC=%08x\n",
          (unsigned long long)host->bar.address,
          (unsigned long long)host->bar.size, version >> 16, hcs1, hcs2, hcc);
  unsigned cap_length = version & 255;
  host->slot_count = hcs1 & 255;
  host->port_count = hcs1 >> 24;
  host->context_size = (hcc & 4) ? 64 : 32;
  host->address_limit = (hcc & 1) ? UINT64_MAX : UINT_MAX;
  if ((version >> 16) < 0x100 || cap_length < 32 || (cap_length & 3) ||
      host->slot_count == 0 || host->port_count == 0 ||
      ((hcs1 >> 8) & 0x7ff) == 0) {
    return "unsupported controller capabilities";
  }
  host->operational = xhci_map(host, cap_length, 0x40);
  host->ports = xhci_map(host, cap_length + 0x400, host->port_count * 16);
  host->doorbells =
      xhci_map(host, xhci_read(cap, 20) & ~3u, (host->slot_count + 1) * 4);
  host->interrupter =
      xhci_map(host, (uint64_t)(xhci_read(cap, 24) & ~31u) + 32, 32);
  if (host->operational == NULL || host->ports == NULL ||
      host->doorbells == NULL || host->interrupter == NULL) {
    return "register window mapping";
  }
  host->slots = malloc((host->slot_count + 1) * sizeof(*host->slots));
  if (host->slots == NULL) {
    return "slot table allocation";
  }
  memset(host->slots, 0, (host->slot_count + 1) * sizeof(*host->slots));
  host->root_ports = malloc(host->port_count * sizeof(*host->root_ports));
  if (host->root_ports == NULL) {
    return "root port allocation";
  }
  memset(host->root_ports, 0, host->port_count * sizeof(*host->root_ports));
  if (!xhci_capabilities(host, (hcc >> 16) * 4)) {
    return "extended capabilities / firmware ownership";
  }
  xhci_write(host->operational, XHCI_COMMAND, 0);
  if (!xhci_wait_register(host->operational, XHCI_STATUS,
                          XHCI_HALTED | XHCI_NOT_READY, XHCI_HALTED)) {
    return "controller halt";
  }
  host->halted = true;
  xhci_write(host->operational, XHCI_COMMAND, XHCI_RESET);
  if (!xhci_wait_register(host->operational, XHCI_COMMAND, XHCI_RESET, 0) ||
      !xhci_wait_register(host->operational, XHCI_STATUS, XHCI_NOT_READY, 0) ||
      !(xhci_read(host->operational, XHCI_PAGE_SIZES) & 1)) {
    return "controller reset / 4K page support";
  }
  if (!xhci_dma_allocate(host, &host->dcbaa, (host->slot_count + 1) * 8, 64) ||
      !xhci_dma_allocate(host, &host->erst, 16, 64) ||
      !xhci_ring_allocate(host, &host->commands) ||
      !xhci_ring_allocate(host, &host->events)) {
    return "DMA rings allocation";
  }
  unsigned scratch_count = (hcs2 >> 27) | ((hcs2 >> 16) & 0x3e0);
  if (scratch_count != 0) {
    if (!xhci_dma_allocate(host, &host->scratch_array, scratch_count * 8, 64) ||
        !xhci_dma_allocate(host, &host->scratch_pages,
                           scratch_count * XHCI_PAGE_SIZE, XHCI_PAGE_SIZE)) {
      return "scratchpad allocation";
    }
    uint64_t *scratch = host->scratch_array.data;
    for (unsigned i = 0; i < scratch_count; i++) {
      scratch[i] = host->scratch_pages.address + i * XHCI_PAGE_SIZE;
    }
    *(uint64_t *)host->dcbaa.data = host->scratch_array.address;
    dma_sync_for_device(scratch, scratch_count * 8);
  }
  xhci_event_segment_t *erst = host->erst.data;
  *erst = (xhci_event_segment_t){.address = host->events.memory.address,
                                 .count = XHCI_RING_SIZE};
  dma_sync_for_device(erst, sizeof(*erst));
  dma_sync_for_device(host->dcbaa.data, (host->slot_count + 1) * 8);
  xhci_write(host->operational, XHCI_CONFIG, host->slot_count);
  xhci_write64(host->operational, XHCI_DCBAA, host->dcbaa.address);
  xhci_write64(host->operational, XHCI_COMMAND_RING,
               host->commands.memory.address | 1);
  xhci_write(host->interrupter, XHCI_ERST_SIZE, 1);
  xhci_write64(host->interrupter, XHCI_ERST_BASE, host->erst.address);
  xhci_write64(host->interrupter, XHCI_EVENT_DEQUEUE,
               host->events.memory.address);
  /* BOT stages are serialized: do not add a delay to every completion. */
  xhci_write(host->interrupter, XHCI_IMOD, 0);
  if (!pci_irq_initialize(host->pci, xhci_interrupt, &host->interrupt)) {
    return "PCI interrupt setup (MSI-X/MSI/INTx)";
  }
  pci_command_update(host->pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0);
  host->running = true;
  host->halted = false;
  xhci_write(host->interrupter, XHCI_IMAN, 3);
  xhci_write(host->operational, XHCI_COMMAND,
             XHCI_RUN | XHCI_INTERRUPTS | XHCI_ERROR_INTERRUPTS);
  if (!xhci_wait_register(host->operational, XHCI_STATUS, XHCI_HALTED, 0)) {
    xhci_stop(host);
    return "controller start";
  }
  unsigned connected = 0;
  for (unsigned index = 0; index < host->port_count; index++) {
    xhci_port_t *port = &host->root_ports[index];
    port->usb.number = index + 1;
    port->usb.dirty = true;
    uint32_t status = xhci_read(host->ports, index * 16);
    connected += (status & XHCI_PORT_CONNECTED) != 0;
    xhci_write(host->ports, index * 16,
               (status & (XHCI_PORT_WRITABLE | XHCI_PORT_CHANGES)) |
                   XHCI_PORT_POWER);
  }
  static const char *const transports[] = {"none", "intx", "msi", "msix"};
  usb_log("xhci: %04x:%02x:%02x.%u version=%04x ports=%u slots=%u context=%u "
          "dma=%u irq=%u transport=%s ready\n",
          host->pci->segment, host->pci->bus, host->pci->slot,
          host->pci->function, version >> 16, host->port_count,
          host->slot_count, host->context_size, (hcc & 1) ? 64 : 32,
          host->interrupt.irq, transports[host->interrupt.mode]);
  usb_log("xhci: root ports connected before scan=%u/%u\n", connected,
          host->port_count);
  return NULL;
}

static void xhci_host_release(xhci_host_t *host) {
  pci_irq_release(&host->interrupt);
  xhci_dma_release(host, &host->dcbaa);
  xhci_dma_release(host, &host->erst);
  xhci_dma_release(host, &host->scratch_array);
  xhci_dma_release(host, &host->scratch_pages);
  xhci_dma_release(host, &host->commands.memory);
  xhci_dma_release(host, &host->events.memory);
  free(host->root_ports);
  free(host->slots);
  free(host);
}

void xhci_initialize(void) {
#ifdef KERNEL_USB_DEBUG
  usb_log("usb: screen diagnostics ON; first 8 HID reports traced\n");
#endif
  const pci_device_t *pci = NULL;
  unsigned found = 0, ready = 0;
  while ((pci = pci_find_class(0x0c, 0x03, pci)) != NULL) {
    found++;
    usb_log("usb: PCI %04x:%02x:%02x.%u id=%04x:%04x prog-if=%02x irq=%u\n",
            pci->segment, pci->bus, pci->slot, pci->function, pci->vendor_id,
            pci->device_id, pci->programming_interface,
            pci_interrupt_line(pci));
    if (pci->programming_interface != 0x30) {
      usb_log("usb: controller is not xHCI; no driver for prog-if=%02x\n",
              pci->programming_interface);
      continue;
    }
    xhci_host_t *host = malloc(sizeof(*host));
    if (host == NULL) {
      usb_log("xhci: controller allocation failed\n");
      break;
    }
    memset(host, 0, sizeof(*host));
    host->pci = pci;
    host->next = xhci.hosts;
    xhci.hosts = host;
    const char *error = xhci_host_initialize(host);
    if (error == NULL) {
      ready++;
      continue;
    }
    usb_log("xhci: %04x:%02x:%02x.%u initialization failed: %s\n", pci->segment,
            pci->bus, pci->slot, pci->function, error);
    xhci.hosts = host->next;
    xhci_host_release(host);
  }
  usb_log("usb: PCI USB controllers=%u xHCI ready=%u\n", found, ready);
  if (xhci.hosts == NULL) {
    return;
  }
  xhci.worker = create_task((uintptr_t)xhci_service, 1);
  if (xhci.worker != NULL) {
    strcpy(xhci.worker->name, "usb");
    usb_service_register(xhci.worker->tid);
    if (task_publish(xhci.worker)) {
      return;
    }
    task_abort_creation(xhci.worker);
    xhci.worker = NULL;
  }
  while (xhci.hosts != NULL) {
    xhci_host_t *host = xhci.hosts;
    xhci_stop(host);
    xhci.hosts = host->next;
    xhci_host_release(host);
  }
  usb_log("xhci: could not create USB service task\n");
}
