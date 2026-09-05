#include <dos.h>
#include <irq.h>
#include <usb.h>

/* USB 2.0 chapter 11. USB 3.x hubs expose a separate USB 2.0 hub for
 * low/full/high-speed devices, including keyboards in USB 3.x receptacles. */
enum {
  HUB_DESCRIPTOR = 0x29,
  HUB_GET_STATUS = 0,
  HUB_CLEAR_FEATURE = 1,
  HUB_SET_FEATURE = 3,
  HUB_PORT_POWER = 8,
  HUB_PORT_RESET = 4,
  HUB_CONNECTED = 1u << 0,
  HUB_ENABLED = 1u << 1,
  HUB_OVERCURRENT = 1u << 3,
  HUB_RESETTING = 1u << 4,
  HUB_LOW_SPEED = 1u << 9,
  HUB_HIGH_SPEED = 1u << 10,
  HUB_CONNECTION_CHANGE = 1u << 0,
  HUB_ENABLE_CHANGE = 1u << 1,
  HUB_RESET_CHANGE = 1u << 4,
};

typedef struct usb_hub {
  struct usb_hub *next;
  usb_device_t *device;
  unsigned count;
  bool changed;
  usb_port_t ports[];
} usb_hub_t;

static usb_hub_t *usb_hubs;

static int hub_request(usb_hub_t *hub, unsigned port, unsigned request,
                       unsigned value, void *data, unsigned length) {
  usb_setup_t setup = {.type = (port ? 0x23 : 0x20) | (length ? 0x80 : 0),
                       .request = request,
                       .value = value,
                       .index = port,
                       .length = length};
  return hub->device->ops->control(hub->device, &setup, data);
}

static bool hub_status(usb_hub_t *hub, unsigned port, unsigned *status,
                       unsigned *changes) {
  uint8_t bytes[4];
  if (hub_request(hub, port, HUB_GET_STATUS, 0, bytes, sizeof(bytes)) !=
      sizeof(bytes)) {
    return false;
  }
  *status = bytes[0] | (unsigned)bytes[1] << 8;
  *changes = bytes[2] | (unsigned)bytes[3] << 8;
  /* Only acknowledge defined change features, never status or reserved bits. */
  unsigned mask = *changes & (port ? 0x3f : 3);
  for (unsigned bit = 0; mask != 0; bit++, mask >>= 1) {
    if (!(mask & 1)) {
      continue;
    }
    unsigned feature = port ? (bit == 5 ? 23 : 16 + bit) : bit;
    if (hub_request(hub, port, HUB_CLEAR_FEATURE, feature, NULL, 0) != 0) {
      return false;
    }
  }
  return true;
}

static bool hub_interrupt(void *context, const uint8_t *data, unsigned length) {
  usb_hub_t *hub = context;
  for (unsigned bit = 0; bit <= hub->count && bit / 8 < length; bit++) {
    if (!(data[bit / 8] & (1u << (bit % 8)))) {
      continue;
    }
    if (bit == 0) {
      hub->changed = true;
    } else {
      hub->ports[bit - 1].dirty = true;
    }
  }
  return usb_service_wake();
}

static void hub_detach(usb_interface_t *interface) {
  usb_hub_t *hub = interface->data;
  usb_hub_t **link = &usb_hubs;
  while (*link != NULL && *link != hub) {
    link = &(*link)->next;
  }
  if (*link == hub) {
    *link = hub->next;
  }
  for (unsigned i = 0; i < hub->count; i++) {
    usb_device_t *child = hub->ports[i].device;
    if (child != NULL) {
      child->ops->detach(child);
    }
  }
  free(hub);
}

bool usb_hub_bind(usb_interface_t *interface) {
  usb_device_t *device = interface->device;
  unsigned depth = 0;
  for (usb_port_t *port = device->port; port->hub != NULL;
       port = port->hub->port) {
    depth++;
  }
  if (depth >= 5) { /* USB's seven-tier topology permits five external hubs. */
    usb_log("usb-hub: hub topology exceeds five hubs\n");
    return false;
  }
  if ((device->speed != USB_SPEED_FULL && device->speed != USB_SPEED_HIGH) ||
      interface->protocol > 2 || interface->descriptors[4] != 1) {
    usb_log("usb-hub: %04x:%04x unsupported hub speed=%u protocol=%u\n",
            device->descriptor.vendor, device->descriptor.product,
            device->speed, interface->protocol);
    return false;
  }
  uint8_t descriptor[7 + 2 * 32]; /* 255 ports plus the hub status bit. */
  usb_setup_t setup = {.type = 0xa0,
                       .request = USB_REQUEST_GET_DESCRIPTOR,
                       .value = HUB_DESCRIPTOR << 8,
                       .length = 7};
  int received = device->ops->control(device, &setup, descriptor);
  if (received != 7 || descriptor[1] != HUB_DESCRIPTOR || descriptor[2] == 0) {
    usb_log("usb-hub: descriptor header returned=%d\n", received);
    return false;
  }
  unsigned count = descriptor[2], bitmap = (count + 8) / 8;
  /* PortPwrCtrlMask is reserved; only the header and DeviceRemovable are
   * needed. Bound the full response without assuming the reserved tail size. */
  if (descriptor[0] < 7 + bitmap || descriptor[0] > sizeof(descriptor)) {
    usb_log("usb-hub: invalid descriptor length=%u ports=%u\n", descriptor[0],
            count);
    return false;
  }
  setup.length = descriptor[0];
  received = device->ops->control(device, &setup, descriptor);
  if (received != setup.length || descriptor[0] != setup.length ||
      descriptor[1] != HUB_DESCRIPTOR || descriptor[2] != count) {
    usb_log("usb-hub: descriptor returned=%d expected=%u\n", received,
            setup.length);
    return false;
  }
  unsigned endpoint = 0;
  for (unsigned offset = interface->descriptors[0]; offset < interface->length;
       offset += interface->descriptors[offset]) {
    const uint8_t *entry = interface->descriptors + offset;
    if (entry[1] == USB_DESCRIPTOR_ENDPOINT && (entry[2] & USB_DIRECTION_IN) &&
        (entry[3] & 3) == USB_ENDPOINT_INTERRUPT &&
        ((entry[4] | (unsigned)entry[5] << 8) & 0x7ff) >= bitmap) {
      endpoint = entry[2];
    }
  }
  if (endpoint == 0 || device->ops->hub == NULL ||
      device->ops->attach == NULL || device->ops->detach == NULL) {
    usb_log("usb-hub: missing status endpoint or host operations\n");
    return false;
  }
  usb_hub_t *hub = malloc(sizeof(*hub) + count * sizeof(*hub->ports));
  if (hub == NULL) {
    return false;
  }
  memset(hub, 0, sizeof(*hub) + count * sizeof(*hub->ports));
  hub->device = device;
  hub->count = count;
  hub->changed = true;
  interface->data = hub;
  interface->detach = hub_detach;
  unsigned characteristics = descriptor[3] | (unsigned)descriptor[4] << 8;
  unsigned think_time =
      device->speed == USB_SPEED_HIGH ? (characteristics >> 5) & 3 : 0;
  bool multi_tt = device->speed == USB_SPEED_HIGH && interface->protocol == 2;
  if (!device->ops->hub(device, count, think_time, multi_tt)) {
    return false;
  }
  for (unsigned i = 0; i < count; i++) {
    hub->ports[i] = (usb_port_t){.hub = device, .number = i + 1, .dirty = true};
    if ((characteristics & 3) < 2 &&
        hub_request(hub, i + 1, HUB_SET_FEATURE, HUB_PORT_POWER, NULL, 0) !=
            0) {
      return false;
    }
  }
  unsigned delay = descriptor[5] * 2;
  sleep(delay < 20 ? 20 : delay);
  hub->next = usb_hubs;
  usb_hubs = hub;
  if (!device->ops->interrupt(device, endpoint, hub_interrupt, hub)) {
    return false;
  }
  usb_log("usb-hub: %04x:%04x ports=%u power=%u tt=%s think=%u active\n",
          device->descriptor.vendor, device->descriptor.product, count,
          characteristics & 3,
          device->speed != USB_SPEED_HIGH ? "none"
          : multi_tt                      ? "multi"
                                          : "single",
          think_time);
  return true;
}

static bool hub_port_reset(usb_hub_t *hub, usb_port_t *port, unsigned *status) {
  uint64_t deadline = monotonic_time_ns() + 1000000000ull;
  uint64_t stable = monotonic_time_ns();
  unsigned changes;
  do {
    sleep(10);
    if (!hub_status(hub, port->number, status, &changes) ||
        !(*status & HUB_CONNECTED)) {
      return false;
    }
    if (changes & HUB_CONNECTION_CHANGE) {
      stable = monotonic_time_ns();
    }
    if (monotonic_time_ns() - stable >= 100000000ull) {
      break;
    }
  } while (monotonic_time_ns() < deadline);
  if (monotonic_time_ns() >= deadline ||
      hub_request(hub, port->number, HUB_SET_FEATURE, HUB_PORT_RESET, NULL,
                  0) != 0) {
    return false;
  }
  deadline = monotonic_time_ns() + 1000000000ull;
  do {
    sleep(10);
    if (!hub_status(hub, port->number, status, &changes) ||
        !(*status & HUB_CONNECTED) || (*status & HUB_OVERCURRENT)) {
      return false;
    }
    if ((*status & (HUB_RESETTING | HUB_ENABLED)) == HUB_ENABLED &&
        (changes & HUB_RESET_CHANGE)) {
      sleep(10); /* Reset recovery before Address Device. */
      return true;
    }
  } while (monotonic_time_ns() < deadline);
  return false;
}

bool usb_hub_pending(void) {
  for (usb_hub_t *hub = usb_hubs; hub != NULL; hub = hub->next) {
    if (!usb_connected(hub->device)) {
      continue;
    }
    if (hub->changed) {
      return true;
    }
    for (unsigned i = 0; i < hub->count; i++) {
      if (hub->ports[i].dirty) {
        return true;
      }
    }
  }
  return false;
}

bool usb_hub_service(void) {
  for (usb_hub_t *hub = usb_hubs; hub != NULL; hub = hub->next) {
    if (!usb_connected(hub->device)) {
      continue;
    }
    usb_port_t *port = NULL;
    irq_state_t state = irq_save();
    bool changed = hub->changed;
    hub->changed = false;
    if (!changed) {
      for (unsigned i = 0; i < hub->count; i++) {
        if (hub->ports[i].dirty) {
          port = &hub->ports[i];
          port->dirty = false;
          break;
        }
      }
    }
    irq_restore(state);
    if (!changed && port == NULL) {
      continue;
    }
    unsigned status, changes;
    if (!hub_status(hub, port ? port->number : 0, &status, &changes) ||
        (port == NULL && (status & 2))) {
      usb_log("usb-hub: %04x:%04x status/overcurrent failure\n",
              hub->device->descriptor.vendor, hub->device->descriptor.product);
      hub->device->connected = false;
      hub->device->port->dirty = true;
      return true;
    }
    if (port == NULL) {
      return true;
    }
#ifdef KERNEL_USB_DEBUG
    usb_log("usb-hub: %04x:%04x port=%u status=%04x changes=%04x\n",
            hub->device->descriptor.vendor, hub->device->descriptor.product,
            port->number, status, changes);
#endif
    if (!(status & HUB_CONNECTED) ||
        (changes & (HUB_CONNECTION_CHANGE | HUB_ENABLE_CHANGE))) {
      port->attempted = false;
    }
    if (port->device != NULL &&
        (!usb_connected(port->device) || !(status & HUB_CONNECTED) ||
         !(status & HUB_ENABLED) || (status & HUB_OVERCURRENT) ||
         (changes & HUB_CONNECTION_CHANGE))) {
      usb_log("usb-hub: %04x:%04x port=%u disconnected\n",
              hub->device->descriptor.vendor, hub->device->descriptor.product,
              port->number);
      port->device->ops->detach(port->device);
    }
    if (port->device == NULL && (status & HUB_CONNECTED) &&
        !(status & HUB_OVERCURRENT) && !port->attempted) {
      port->attempted = true;
      if (!hub_port_reset(hub, port, &status)) {
        usb_log("usb-hub: port=%u debounce/reset failed status=%04x\n",
                port->number, status);
        return true;
      }
      usb_speed_t speed = status & HUB_LOW_SPEED    ? USB_SPEED_LOW
                          : status & HUB_HIGH_SPEED ? USB_SPEED_HIGH
                                                    : USB_SPEED_FULL;
      if (!hub->device->ops->attach(port, speed)) {
        usb_log("usb-hub: port=%u enumeration failed\n", port->number);
      }
    }
    /* Attach/detach may change the hub list. Restart on the next service turn.
     */
    return true;
  }
  return false;
}
