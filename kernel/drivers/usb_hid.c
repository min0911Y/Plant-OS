#include <dos.h>
#include <input_device.h>
#include <usb.h>

typedef struct hid_report {
  struct hid_report *next;
  unsigned id, bits;
  uint8_t keys[64];
  uint32_t buttons;
} hid_report_t;

typedef struct hid_field {
  struct hid_field *next;
  hid_report_t *report;
  uint32_t usage, usage_end;
  uint32_t *usages;
  unsigned usage_count;
  int32_t logical_min, logical_max;
  unsigned offset, width, count;
  bool array, signed_value, relative;
} hid_field_t;

typedef struct {
  input_keyboard_t keyboard;
  input_pointer_t mouse;
  hid_report_t *reports;
  hid_field_t *fields;
  bool report_ids, pointer;
#ifdef KERNEL_USB_DEBUG
  usb_input_diagnostic_t diagnostic;
#endif
} usb_hid_t;

typedef struct {
  uint32_t page, width, count, id;
  int32_t minimum, maximum;
} hid_global_t;

typedef struct hid_global_frame {
  struct hid_global_frame *next;
  hid_global_t value;
} hid_global_frame_t;

/* HID Keyboard/Keypad usage -> the kernel's logical key code. */
static const uint16_t hid_keys[256] = {
    [4] = 0x1e,   0x30,        0x2e,  0x20,         0x12,  0x21,        0x22,
    0x23,         0x17,        0x24,  0x25,         0x26,  0x32,        0x31,
    0x18,         0x19,        0x10,  0x13,         0x1f,  0x14,        0x16,
    0x2f,         0x11,        0x2d,  0x15,         0x2c,  [30] = 0x02, 0x03,
    0x04,         0x05,        0x06,  0x07,         0x08,  0x09,        0x0a,
    0x0b,         [40] = 0x1c, 0x01,  0x0e,         0x0f,  0x39,        0x0c,
    0x0d,         0x1a,        0x1b,  0x2b,         0x2b,  0x27,        0x28,
    0x29,         0x33,        0x34,  0x35,         0x3a,  [58] = 0x3b, 0x3c,
    0x3d,         0x3e,        0x3f,  0x40,         0x41,  0x42,        0x43,
    0x44,         0x57,        0x58,  [70] = 0x137, 0x46,  0x145,       0x152,
    0x147,        0x149,       0x153, 0x14f,        0x151, 0x14d,       0x14b,
    0x150,        0x148,       0x45,  0x135,        0x37,  0x4a,        0x4e,
    0x11c,        0x4f,        0x50,  0x51,         0x4b,  0x4c,        0x4d,
    0x47,         0x48,        0x49,  0x52,         0x53,  0x56,        0x15d,
    [224] = 0x1d, 0x2a,        0x38,  0x15b,        0x11d, 0x36,        0x138,
    0x15c,
};

static int32_t hid_value(const uint8_t *data, unsigned offset, unsigned width,
                         bool signed_value) {
  uint64_t value = 0;
  unsigned bytes = ((offset & 7) + width + 7) / 8;
  for (unsigned i = 0; i < bytes; i++) {
    value |= (uint64_t)data[offset / 8 + i] << (8 * i);
  }
  value = (value >> (offset & 7)) & ((1ull << width) - 1);
  if (signed_value && width < 32 && (value & (1u << (width - 1)))) {
    value |= ~((1ull << width) - 1);
  }
  return (int32_t)value;
}

static bool hid_parse(usb_hid_t *hid, const uint8_t *descriptor,
                      unsigned length, unsigned capacity) {
  hid_global_t global = {0};
  hid_global_frame_t *stack = NULL;
  uint32_t *usages = malloc((length / 2 + 1) * sizeof(*usages));
  uint8_t *collections = malloc(length);
  if (usages == NULL || collections == NULL) {
    free(usages);
    free(collections);
    return false;
  }
  unsigned depth = 0, role = 0;
  unsigned usage_count = 0;
  uint32_t usage_min = 0, usage_max = 0;
  bool valid = true;
  for (unsigned offset = 0; valid && offset < length;) {
    uint8_t prefix = descriptor[offset++];
    if (prefix == 0xfe) {
      if (length - offset < 2 || descriptor[offset] > length - offset - 2) {
        valid = false;
        break;
      }
      offset += 2 + descriptor[offset];
      continue;
    }
    unsigned bytes = prefix & 3;
    bytes = bytes == 3 ? 4 : bytes;
    if (bytes > length - offset) {
      valid = false;
      break;
    }
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; i++) {
      value |= (uint32_t)descriptor[offset + i] << (8 * i);
    }
    int32_t signed_value = bytes == 1   ? (int8_t)value
                           : bytes == 2 ? (int16_t)value
                                        : (int32_t)value;
    offset += bytes;
    unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (type == 1) {
      switch (tag) {
      case 0:
        global.page = value;
        break;
      case 1:
        global.minimum = signed_value;
        break;
      case 2:
        global.maximum = global.minimum < 0 ? signed_value : (int32_t)value;
        break;
      case 7:
        global.width = value;
        break;
      case 8:
        valid = value > 0 && value < 256;
        global.id = value;
        hid->report_ids = true;
        break;
      case 9:
        global.count = value;
        break;
      case 10: {
        hid_global_frame_t *frame = malloc(sizeof(*frame));
        if (frame == NULL) {
          valid = false;
          break;
        }
        *frame = (hid_global_frame_t){.next = stack, .value = global};
        stack = frame;
        break;
      }
      case 11:
        if (stack == NULL) {
          valid = false;
        } else {
          hid_global_frame_t *frame = stack;
          global = frame->value;
          stack = frame->next;
          free(frame);
        }
        break;
      }
      continue;
    }
    if (type == 2) {
      if (tag <= 2 && bytes == 0) {
        valid = false;
        break;
      }
      uint32_t usage = bytes == 4 ? value : (global.page << 16) | value;
      if (tag == 0) {
        usages[usage_count++] = usage;
      } else if (tag == 1) {
        usage_min = usage;
      } else if (tag == 2) {
        usage_max = usage;
      }
      continue;
    }
    if (type != 0) {
      continue;
    }
    if (tag == 10) {
      collections[depth++] = role;
      if (value == 1) {
        uint32_t usage = usage_count ? usages[0] : usage_min;
        role = usage == 0x10006 || usage == 0x10007 ? 1
               : usage == 0x10002                   ? 2
                                                    : 0;
      }
    } else if (tag == 12) {
      if (depth == 0)
        valid = false;
      else
        role = collections[--depth];
    } else if (tag == 8) {
      hid_report_t *report = hid->reports;
      while (report != NULL && report->id != global.id) {
        report = report->next;
      }
      if (report == NULL) {
        report = malloc(sizeof(*report));
        if (report == NULL) {
          valid = false;
          break;
        }
        *report = (hid_report_t){.id = global.id, .next = hid->reports};
        hid->reports = report;
      }
      if (global.width == 0 || global.width > 32 ||
          global.count > (capacity * 8 - report->bits) / global.width) {
        valid = false;
        break;
      }
      unsigned bits = global.count * global.width;
      bool array = !(value & 2);
      unsigned fields = array ? 1 : global.count;
      for (unsigned i = 0; !(value & 1) && valid && i < fields; i++) {
        uint32_t usage =
            usage_count != 0
                ? usages[i < usage_count ? i : usage_count - 1]
                : usage_min +
                      (i <= usage_max - usage_min ? i : usage_max - usage_min);
        bool keyboard = role == 1 && (usage >> 16) == 7;
        bool pointer = role == 2 && ((usage >> 16) == 9 || usage == 0x10030 ||
                                     usage == 0x10031 || usage == 0x10038);
        if (pointer && (usage == 0x10030 || usage == 0x10031) && !(value & 4)) {
          valid =
              false; /* Absolute tablets need a separate pointer contract. */
          break;
        }
        if ((!keyboard && !pointer) ||
            (array && !keyboard && (usage >> 16) != 9)) {
          continue;
        }
        hid_field_t *field = malloc(sizeof(*field));
        if (field == NULL) {
          valid = false;
          break;
        }
        *field = (hid_field_t){.next = hid->fields,
                               .report = report,
                               .offset = report->bits + i * global.width,
                               .width = global.width,
                               .count = array ? global.count : 1,
                               .array = array,
                               .usage = usage,
                               .usage_end = usage_max,
                               .logical_min = global.minimum,
                               .logical_max = global.maximum,
                               .signed_value = global.minimum < 0,
                               .relative = (value & 4) != 0};
        if (array && usage_count != 0) {
          field->usages = malloc(usage_count * sizeof(*usages));
          if (field->usages == NULL) {
            free(field);
            valid = false;
            break;
          }
          memcpy(field->usages, usages, usage_count * sizeof(*usages));
          field->usage_count = usage_count;
        }
        hid->fields = field;
        hid->pointer |= pointer;
      }
      report->bits += bits;
    }
    usage_count = 0;
    usage_min = usage_max = 0;
  }
  while (stack != NULL) {
    hid_global_frame_t *frame = stack;
    stack = frame->next;
    free(frame);
  }
  free(usages);
  free(collections);
  valid &= depth == 0;
  for (hid_report_t *report = hid->reports; report != NULL;
       report = report->next) {
    valid &= report->bits + (hid->report_ids ? 8 : 0) <= capacity * 8 &&
             (!hid->report_ids || report->id != 0);
  }
  return valid && hid->fields != NULL;
}

#ifdef KERNEL_USB_DEBUG
static bool hid_trace_input(usb_hid_t *hid, unsigned length, unsigned id,
                            const hid_report_t *report, unsigned changes,
                            bool wake) {
  /* Keep typing responsive and do not turn diagnostics into a key logger. */
  usb_input_diagnostic_t *event = &hid->diagnostic;
  if (event->sequence == 8) {
    return wake;
  }
  event->sequence++;
  event->length = length;
  event->id = id;
  event->expected = report == NULL ? 0 : (report->bits + 7) / 8 + hid->report_ids;
  event->valid = report != NULL && length != 0 && length >= event->expected;
  event->changes = changes;
  event->wake = wake;
  event->pointer = hid->pointer;
  return usb_debug_input(event) || wake;
}
#endif

static bool hid_input(void *context, const uint8_t *data, unsigned length) {
  usb_hid_t *hid = context;
  unsigned id = hid->report_ids && length != 0 ? data[0] : 0;
  hid_report_t *report = hid->reports;
  while (report != NULL && report->id != id) {
    report = report->next;
  }
  if (length == 0 || report == NULL ||
      report->bits > (length - hid->report_ids) * 8) {
#ifdef KERNEL_USB_DEBUG
    return hid_trace_input(hid, length, id, report, 0, false);
#else
    return false;
#endif
  }
  data += hid->report_ids;
  uint8_t keys[64] = {0};
  mouse_event_t mouse = {0};
  bool rollover = false, pointer = false;
  for (hid_field_t *field = hid->fields; field != NULL; field = field->next) {
    if (field->report != report) {
      continue;
    }
    for (unsigned i = 0; i < field->count; i++) {
      int32_t value = hid_value(data, field->offset + i * field->width,
                                field->width, field->signed_value);
      uint32_t usage = field->usage;
      if (field->array) {
        if (value < field->logical_min || value > field->logical_max)
          continue;
        uint32_t index = (uint32_t)value - (uint32_t)field->logical_min;
        if (field->usages != NULL) {
          if (index >= field->usage_count)
            continue;
          usage = field->usages[index];
        } else {
          if (field->usage_end < usage || index > field->usage_end - usage)
            continue;
          usage += index;
        }
        value = 1;
      }
      unsigned page = usage >> 16, code = usage & 0xffff;
      if (page == 7 && value != 0 && code < 256) {
        if (code >= 1 && code <= 3) {
          rollover = true;
        }
        unsigned key = hid_keys[code];
        if (key != 0) {
          keys[key / 8] |= 1u << (key % 8);
        }
      } else if (page == 9 && code >= 1 && code <= 32) {
        mouse.buttons |= value != 0 ? 1u << (code - 1) : 0;
        pointer = true;
      } else if (page == 1 && field->relative) {
        if (code == 0x30)
          mouse.x = value;
        if (code == 0x31)
          mouse.y = value;
        if (code == 0x38)
          mouse.wheel = value;
        pointer = true;
      }
    }
  }
  if (!rollover) {
    memcpy(report->keys, keys, sizeof(keys));
  }
  memset(keys, 0, sizeof(keys));
  for (hid_report_t *other = hid->reports; other != NULL; other = other->next) {
    for (unsigned i = 0; i < sizeof(keys); i++) {
      keys[i] |= other->keys[i];
    }
  }
  bool reschedule = false;
#ifdef KERNEL_USB_DEBUG
  unsigned changes = 0;
#endif
  static const uint16_t modifiers[] = {0x1d,  0x2a,  0x36,  0x38,
                                       0x11d, 0x138, 0x15b, 0x15c};
  for (unsigned i = 0; i < sizeof(modifiers) / sizeof(*modifiers); i++) {
    unsigned code = modifiers[i], bit = 1u << (code % 8);
    if ((keys[code / 8] & bit) && !(hid->keyboard.down[code / 8] & bit)) {
#ifdef KERNEL_USB_DEBUG
      changes++;
#endif
      reschedule |= input_keyboard_event(&hid->keyboard, code, true);
    }
  }
  for (unsigned byte = 0; byte < sizeof(keys); byte++) {
    unsigned changed = keys[byte] ^ hid->keyboard.down[byte];
    while (changed != 0) {
      unsigned bit = __builtin_ctz(changed), code = byte * 8 + bit;
      changed &= changed - 1;
      bool modifier = false;
      for (unsigned i = 0; i < sizeof(modifiers) / sizeof(*modifiers); i++) {
        modifier |= modifiers[i] == code;
      }
      if (!modifier) {
#ifdef KERNEL_USB_DEBUG
        changes++;
#endif
        reschedule |= input_keyboard_event(&hid->keyboard, code,
                                           (keys[byte] & (1u << bit)) != 0);
      }
    }
  }
  for (unsigned i = 0; i < sizeof(modifiers) / sizeof(*modifiers); i++) {
    unsigned code = modifiers[i], bit = 1u << (code % 8);
    if (!(keys[code / 8] & bit) && (hid->keyboard.down[code / 8] & bit)) {
#ifdef KERNEL_USB_DEBUG
      changes++;
#endif
      reschedule |= input_keyboard_event(&hid->keyboard, code, false);
    }
  }
  if (pointer) {
    report->buttons = mouse.buttons;
    for (hid_report_t *other = hid->reports; other != NULL;
         other = other->next) {
      mouse.buttons |= other->buttons;
    }
    reschedule |= input_mouse_event(&hid->mouse, &mouse);
  }
#ifdef KERNEL_USB_DEBUG
  return hid_trace_input(hid, length, id, report, changes, reschedule);
#else
  return reschedule;
#endif
}

static void hid_detach(usb_interface_t *interface) {
  usb_hid_t *hid = interface->data;
  input_keyboard_release(&hid->keyboard);
  if (hid->pointer) {
    mouse_event_t released = {0};
    input_mouse_event(&hid->mouse, &released);
  }
  while (hid->fields != NULL) {
    hid_field_t *field = hid->fields;
    hid->fields = field->next;
    free(field->usages);
    free(field);
  }
  while (hid->reports != NULL) {
    hid_report_t *report = hid->reports;
    hid->reports = report->next;
    free(report);
  }
  free(hid);
  interface->data = NULL;
  interface->detach = NULL;
}

bool usb_hid_bind(usb_interface_t *interface) {
  unsigned report_length = 0, endpoint = 0, capacity = 0;
  for (unsigned offset = 0; offset < interface->length;) {
    const uint8_t *entry = interface->descriptors + offset;
    if (entry[1] == USB_DESCRIPTOR_HID && entry[0] >= 6) {
      for (unsigned i = 0; i < entry[5] && 6 + i * 3 + 3 <= entry[0]; i++) {
        if (entry[6 + i * 3] == USB_DESCRIPTOR_REPORT) {
          report_length = entry[7 + i * 3] | (unsigned)entry[8 + i * 3] << 8;
        }
      }
    }
    if (entry[1] == USB_DESCRIPTOR_ENDPOINT && (entry[2] & USB_DIRECTION_IN) &&
        (entry[3] & 3) == USB_ENDPOINT_INTERRUPT) {
      endpoint = entry[2];
      unsigned packet = entry[4] | (unsigned)entry[5] << 8;
      capacity = (packet & 0x7ff) * (((packet >> 11) & 3) + 1);
      if (interface->device->speed == USB_SPEED_SUPER) {
        const uint8_t *companion = entry + entry[0];
        capacity = companion[4] | (unsigned)companion[5] << 8;
      }
    }
    offset += entry[0];
  }
  if (report_length == 0 || endpoint == 0 || capacity == 0) {
    usb_log("usb-hid: if=%u missing report/interrupt-IN descriptor "
            "report=%u ep=%02x packet=%u\n",
            interface->number, report_length, endpoint, capacity);
    return false;
  }
  usb_hid_t *hid = malloc(sizeof(*hid));
  uint8_t *descriptor = malloc(report_length);
  if (hid == NULL || descriptor == NULL) {
    free(hid);
    free(descriptor);
    usb_log("usb-hid: if=%u descriptor allocation failed\n", interface->number);
    return false;
  }
  memset(hid, 0, sizeof(*hid));
  hid->keyboard.software_repeat = true;
  interface->data = hid;
  interface->detach = hid_detach;
  usb_device_t *device = interface->device;
#ifdef KERNEL_USB_DEBUG
  hid->diagnostic = (usb_input_diagnostic_t){
      .vendor = device->descriptor.vendor,
      .product = device->descriptor.product,
      .interface = interface->number,
  };
#endif
  usb_setup_t setup = {.type = 0x81,
                       .request = USB_REQUEST_GET_DESCRIPTOR,
                       .value = USB_DESCRIPTOR_REPORT << 8,
                       .index = interface->number,
                       .length = report_length};
  int result = device->ops->control(device, &setup, descriptor);
  bool valid = result == (int)report_length;
  const char *stage = "report descriptor transfer";
  if (valid) {
    stage = "report descriptor parse";
    valid = hid_parse(hid, descriptor, report_length, capacity);
    result = valid ? 0 : USB_ERROR_IO;
  }
  free(descriptor);
  if (valid && interface->subclass == 1) {
    stage = "SET_PROTOCOL(report)";
    setup = (usb_setup_t){
        .type = 0x21, .request = 11, .value = 1, .index = interface->number};
    result = device->ops->control(device, &setup, NULL);
    valid = result == 0;
  }
  if (valid) {
    stage = "SET_IDLE";
    setup =
        (usb_setup_t){.type = 0x21, .request = 10, .index = interface->number};
    result = device->ops->control(device, &setup, NULL);
    valid = result == 0 || result == USB_ERROR_STALL;
  }
  if (valid) {
    stage = "interrupt-IN submission";
    valid = device->ops->interrupt(device, endpoint, hid_input, hid);
    result = valid ? 0 : USB_ERROR_IO;
  }
  if (!valid) {
    usb_log("usb-hid: %04x:%04x if=%u %s failed result=%d report=%u packet=%u\n",
            device->descriptor.vendor, device->descriptor.product,
            interface->number, stage, result, report_length, capacity);
    hid_detach(interface);
    return false;
  }
  usb_log("usb-hid: interface=%u endpoint=%02x active\n", interface->number,
       endpoint);
  unsigned reports = 0, keyboard_fields = 0;
  for (hid_report_t *report = hid->reports; report != NULL; report = report->next) {
    reports++;
  }
  for (hid_field_t *field = hid->fields; field != NULL; field = field->next) {
    keyboard_fields += (field->usage >> 16) == 7;
  }
  usb_log("usb-hid: %04x:%04x if=%u reports=%u keyboard-fields=%u "
          "pointer=%u report-ids=%u\n",
          device->descriptor.vendor, device->descriptor.product,
          interface->number, reports, keyboard_fields, hid->pointer,
          hid->report_ids);
  return true;
}
