#include <dos.h>
#include <io.h>
#include <irq.h>
#include <stdint.h>
#include <usb.h>

void usb_log(const char *format, ...) {
  char buffer[256];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  kprint(buffer);
#ifdef KERNEL_USB_DEBUG
  print(buffer);
#endif
}

bool usb_connected(const usb_device_t *device) {
  for (; device != NULL; device = device->port->hub) {
    if (!device->connected) {
      return false;
    }
  }
  return true;
}

static int usb_descriptor(usb_device_t *device, uint8_t type, void *data,
                          uint16_t length) {
  usb_setup_t setup = {
      .type = USB_DIRECTION_IN,
      .request = USB_REQUEST_GET_DESCRIPTOR,
      .value = (uint16_t)type << 8,
      .length = length,
  };
  int result = device->ops->control(device, &setup, data);
  if (result != length) {
    usb_log("usb: descriptor type=%u length=%u returned=%d\n", type, length,
            result);
  }
  return result;
}

bool usb_enumerate(usb_device_t *device) {
  usb_device_descriptor_t *descriptor = &device->descriptor;
  if (usb_descriptor(device, USB_DESCRIPTOR_DEVICE, descriptor, 8) != 8 ||
      descriptor->length != sizeof(*descriptor) ||
      descriptor->type != USB_DESCRIPTOR_DEVICE) {
    return false;
  }
  uint16_t packet_size = descriptor->max_packet_size;
  switch (device->speed) {
  case USB_SPEED_LOW:
    if (packet_size != 8) {
      return false;
    }
    break;
  case USB_SPEED_FULL:
    if (packet_size != 8 && packet_size != 16 && packet_size != 32 &&
        packet_size != 64) {
      return false;
    }
    break;
  case USB_SPEED_HIGH:
    if (packet_size != 64) {
      return false;
    }
    break;
  case USB_SPEED_SUPER:
    if (packet_size != 9) {
      return false;
    }
    packet_size = 512;
    break;
  default:
    return false;
  }
  if (!device->ops->set_packet_size(device, packet_size) ||
      usb_descriptor(device, USB_DESCRIPTOR_DEVICE, descriptor,
                     sizeof(*descriptor)) != sizeof(*descriptor) ||
      descriptor->length != sizeof(*descriptor) ||
      descriptor->type != USB_DESCRIPTOR_DEVICE ||
      descriptor->configurations == 0) {
    return false;
  }

  uint8_t header[9];
  if (usb_descriptor(device, USB_DESCRIPTOR_CONFIGURATION, header,
                     sizeof(header)) != sizeof(header) ||
      header[0] != sizeof(header) ||
      header[1] != USB_DESCRIPTOR_CONFIGURATION || header[5] == 0) {
    return false;
  }
  uint16_t length = header[2] | (uint16_t)header[3] << 8;
  if (length < sizeof(header)) {
    return false;
  }
  uint8_t *configuration = malloc(length);
  if (configuration == NULL) {
    return false;
  }
  bool valid = usb_descriptor(device, USB_DESCRIPTOR_CONFIGURATION,
                              configuration, length) == length &&
               memcmp(configuration, header, sizeof(header)) == 0;
  unsigned interfaces = 0, endpoints = 0, expected_endpoints = 0;
  uint8_t seen[32] = {0};
  bool in_interface = false;
  for (size_t offset = sizeof(header); valid && offset < length;) {
    size_t remaining = length - offset;
    uint8_t *entry = configuration + offset;
    if (remaining < 2 || entry[0] < 2 || entry[0] > remaining) {
      valid = false;
      break;
    }
    if (entry[1] == USB_DESCRIPTOR_INTERFACE) {
      valid =
          entry[0] >= 9 && (!in_interface || endpoints == expected_endpoints);
      if (valid) {
        in_interface = true;
        endpoints = 0;
        expected_endpoints = entry[4];
        if (entry[3] == 0) {
          unsigned bit = 1u << (entry[2] % 8);
          valid = (seen[entry[2] / 8] & bit) == 0;
          seen[entry[2] / 8] |= bit;
          interfaces++;
        }
      }
    } else if (entry[1] == USB_DESCRIPTOR_ENDPOINT) {
      valid =
          entry[0] >= 7 && in_interface && ++endpoints <= expected_endpoints;
    }
    offset += entry[0];
  }
  if (!valid || interfaces != header[4] || endpoints != expected_endpoints) {
    free(configuration);
    return false;
  }
  device->configuration = configuration;
  device->configuration_size = length;
  return true;
}

/* Class binding uses interface boundaries from the validated configuration.
 * Only alternate setting zero is selected; unsupported interfaces stay idle. */
bool usb_bind(usb_device_t *device) {
  usb_endpoint_t endpoints[30]; /* xHCI DCIs 2..31, as defined by USB. */
  unsigned count = 0;
  usb_interface_t **tail = &device->interfaces;
  for (unsigned offset = 9; offset < device->configuration_size;) {
    const uint8_t *descriptor = device->configuration + offset;
    if (descriptor[1] != USB_DESCRIPTOR_INTERFACE) {
      offset += descriptor[0];
      continue;
    }
    unsigned end = offset + descriptor[0];
    while (end < device->configuration_size &&
           device->configuration[end + 1] != USB_DESCRIPTOR_INTERFACE) {
      end += device->configuration[end];
    }
    bool supported =
        descriptor[3] == 0 &&
        (descriptor[5] == 3 || descriptor[5] == 9 ||
         (descriptor[5] == 8 && descriptor[6] == 6 && descriptor[7] == 0x50));
    if (descriptor[3] == 0) {
      usb_log("usb: %04x:%04x if=%u class=%02x/%02x/%02x %s\n",
              device->descriptor.vendor, device->descriptor.product,
              descriptor[2], descriptor[5], descriptor[6], descriptor[7],
              supported ? "binding" : "unsupported interface");
    }
    if (supported) {
      usb_interface_t *interface = malloc(sizeof(*interface));
      if (interface == NULL) {
        return false;
      }
      *interface = (usb_interface_t){.device = device,
                                     .descriptors = descriptor,
                                     .length = end - offset,
                                     .number = descriptor[2],
                                     .class_code = descriptor[5],
                                     .subclass = descriptor[6],
                                     .protocol = descriptor[7]};
      *tail = interface;
      tail = &interface->next;
      for (unsigned pos = offset + descriptor[0]; pos < end;
           pos += device->configuration[pos]) {
        const uint8_t *entry = device->configuration + pos;
        if (entry[1] != USB_DESCRIPTOR_ENDPOINT) {
          continue;
        }
        if (count == sizeof(endpoints) / sizeof(*endpoints) ||
            (entry[3] & 3) < USB_ENDPOINT_BULK || (entry[2] & 15) == 0) {
          return false;
        }
        usb_endpoint_t *ep = &endpoints[count++];
        uint16_t packet = entry[4] | (uint16_t)entry[5] << 8;
        *ep = (usb_endpoint_t){.address = entry[2],
                               .type = entry[3] & 3,
                               .packet_size = packet & 0x7ff,
                               .interval = entry[6],
                               .burst = (packet >> 11) & 3};
        if (device->speed == USB_SPEED_SUPER) {
          const uint8_t *companion = entry + entry[0];
          if (pos + entry[0] + 6 > end || companion[0] < 6 ||
              companion[1] != USB_DESCRIPTOR_SS_ENDPOINT) {
            return false;
          }
          ep->burst = companion[2];
          ep->mult = companion[3] & 3;
          ep->bytes_per_interval = companion[4] | (uint16_t)companion[5] << 8;
        }
      }
    }
    offset = end;
  }
  if (device->interfaces == NULL) {
    return true;
  }
  if (!device->ops->configure(device, endpoints, count)) {
    usb_log("usb: %04x:%04x endpoint configuration failed count=%u\n",
            device->descriptor.vendor, device->descriptor.product, count);
    return false;
  }
  usb_setup_t setup = {.request = 9, .value = device->configuration[5]};
  int result = device->ops->control(device, &setup, NULL);
  if (result != 0) {
    usb_log("usb: %04x:%04x SET_CONFIGURATION %u failed result=%d\n",
            device->descriptor.vendor, device->descriptor.product, setup.value,
            result);
    return false;
  }
  for (usb_interface_t *interface = device->interfaces; interface != NULL;
       interface = interface->next) {
    bool bound = interface->class_code == 3   ? usb_hid_bind(interface)
                 : interface->class_code == 9 ? usb_hub_bind(interface)
                                              : usb_storage_bind(interface);
    if (!bound) {
      usb_log("usb: interface %u class=%02x unavailable\n", interface->number,
              interface->class_code);
      if (interface->class_code == 9) {
        return false;
      }
    }
  }
  return true;
}

void usb_disconnect(usb_device_t *device) {
  device->connected = false;
  while (device->interfaces != NULL) {
    usb_interface_t *interface = device->interfaces;
    device->interfaces = interface->next;
    if (interface->detach != NULL) {
      interface->detach(interface);
    }
    free(interface);
  }
}

static usb_work_t *usb_work_list;
static uint32_t usb_service_tid;

bool usb_service_wake(void) {
  irq_state_t state = irq_save();
  mtask *worker = get_task(usb_service_tid);
  bool wake = worker != NULL && worker->state == WAITING &&
              worker->wait_reason == WAIT_REASON_USB;
  if (wake) {
    task_run(worker);
  }
  irq_restore(state);
  return wake;
}

#ifdef KERNEL_USB_DEBUG
static struct {
  usb_input_diagnostic_t events[16];
  unsigned head, count, dropped;
} usb_input_trace;

bool usb_debug_input(const usb_input_diagnostic_t *event) {
  irq_state_t state = irq_save();
  unsigned capacity = sizeof(usb_input_trace.events) / sizeof(*event);
  if (usb_input_trace.count < capacity) {
    unsigned index =
        (usb_input_trace.head + usb_input_trace.count++) % capacity;
    usb_input_trace.events[index] = *event;
  } else {
    usb_input_trace.dropped++;
  }
  bool wake = usb_service_wake();
  irq_restore(state);
  return wake;
}
#endif

void usb_service_register(uint32_t tid) { usb_service_tid = tid; }

bool usb_work_pending(void) {
  if (usb_hub_pending()) {
    return true;
  }
#ifdef KERNEL_USB_DEBUG
  if (usb_input_trace.count != 0) {
    return true;
  }
#endif
  for (usb_work_t *work = usb_work_list; work != NULL; work = work->next) {
    if (work->state == USB_WORK_QUEUED) {
      return true;
    }
  }
  return false;
}

void usb_work_release(usb_work_t *work) {
  usb_work_t **link = &usb_work_list;
  while (*link != NULL && *link != work) {
    link = &(*link)->next;
  }
  if (*link == work) {
    *link = work->next;
  }
  work->destroy(work);
}

void usb_cancel_task(uint32_t tid, uint32_t generation) {
  usb_work_t *work = usb_work_list;
  while (work != NULL) {
    usb_work_t *next = work->next;
    if (work->tid == tid && work->generation == generation) {
      work->tid = UINT32_MAX;
      if (work->state != USB_WORK_ACTIVE) {
        usb_work_release(work);
      }
    }
    work = next;
  }
}

bool usb_service_work(void) {
  if (usb_hub_service()) {
    return true;
  }
#ifdef KERNEL_USB_DEBUG
  irq_state_t state = irq_save();
  if (usb_input_trace.count != 0) {
    usb_input_diagnostic_t event = usb_input_trace.events[usb_input_trace.head];
    usb_input_trace.head = (usb_input_trace.head + 1) %
                           (sizeof(usb_input_trace.events) / sizeof(event));
    usb_input_trace.count--;
    unsigned dropped = usb_input_trace.dropped;
    usb_input_trace.dropped = 0;
    irq_restore(state);
    usb_log("usb-input: %04x:%04x if=%u ptr=%u n=%u len=%u id=%u need=%u "
            "ok=%u keys=%u wake=%u\n",
            event.vendor, event.product, event.interface, event.pointer,
            event.sequence, event.length, event.id, event.expected, event.valid,
            event.changes, event.wake);
    if (dropped != 0) {
      usb_log("usb-input: %u diagnostic records dropped\n", dropped);
    }
    return true;
  }
  irq_restore(state);
#endif
  usb_work_t *work = usb_work_list;
  while (work != NULL && work->state != USB_WORK_QUEUED) {
    work = work->next;
  }
  if (work == NULL) {
    return false;
  }
  work->state = USB_WORK_ACTIVE;
  work->execute(work);
  work->state = USB_WORK_DONE;
  mtask *waiter = get_task(work->tid);
  if (waiter != NULL && waiter->generation == work->generation) {
    task_run(waiter);
  } else {
    usb_work_release(work);
  }
  return true;
}

void usb_work_submit(usb_work_t *work) {
  mtask *task = current_task();
  work->tid = task->tid;
  work->generation = task->generation;
  work->state = USB_WORK_QUEUED;
  work->next = NULL;
  usb_work_t **tail = &usb_work_list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = work;
  if (task->tid == usb_service_tid) {
    work->state = USB_WORK_ACTIVE;
    work->execute(work);
    work->state = USB_WORK_DONE;
    return;
  }
  usb_service_wake();
  while (work->state != USB_WORK_DONE) {
    task_fall_blocked_reason(WAITING, WAIT_REASON_USB);
  }
}
