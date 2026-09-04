#include <arch/x86/io.h>
#include <dos.h>
#include <limits.h>
#include <pci.h>

enum {
  PCI_CONFIG_ADDRESS_PORT = 0x0cf8,
  PCI_CONFIG_DATA_PORT = 0x0cfc,
  PCI_BUS_COUNT = 256,
  PCI_SLOT_COUNT = 32,
  PCI_FUNCTION_COUNT = 8,
  PCI_DEVICE_LIMIT = PCI_BUS_COUNT * PCI_SLOT_COUNT * PCI_FUNCTION_COUNT,
  PCI_INITIAL_CAPACITY = 16,
  PCI_HEADER_MULTIFUNCTION = 0x80,
  PCI_HEADER_LAYOUT_MASK = 0x7f,
  PCI_CLASS_BRIDGE = 0x06,
  PCI_SUBCLASS_PCI_BRIDGE = 0x04,
  PCI_SUBCLASS_SEMITRANSPARENT_BRIDGE = 0x09,
};

typedef struct {
  pci_device_t *devices;
  size_t count;
  size_t capacity;
  uint8_t visited_buses[PCI_BUS_COUNT];
  bool initialized;
} pci_registry_t;

static pci_registry_t pci_registry;

static uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t function,
                                uint8_t offset) {
  uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                     ((uint32_t)slot << 11) | ((uint32_t)function << 8) |
                     (offset & 0xfcu);
  x86_port_write32(PCI_CONFIG_ADDRESS_PORT, address);
  return x86_port_read32(PCI_CONFIG_DATA_PORT) >> ((offset & 3u) * 8u);
}

static bool pci_registry_append(uint8_t bus, uint8_t slot, uint8_t function,
                                uint32_t identity, uint32_t class_register,
                                uint8_t header_type) {
  if (pci_registry.count == pci_registry.capacity) {
    if (pci_registry.capacity == PCI_DEVICE_LIMIT) {
      return false;
    }
    size_t capacity = pci_registry.capacity == 0
                          ? PCI_INITIAL_CAPACITY
                          : pci_registry.capacity * 2;
    if (capacity > PCI_DEVICE_LIMIT) {
      capacity = PCI_DEVICE_LIMIT;
    }
    pci_device_t *devices =
        realloc(pci_registry.devices, capacity * sizeof(*devices));
    if (devices == NULL) {
      return false;
    }
    pci_registry.devices = devices;
    pci_registry.capacity = capacity;
  }

  pci_registry.devices[pci_registry.count++] = (pci_device_t){
      .vendor_id = identity,
      .device_id = identity >> 16,
      .bus = bus,
      .slot = slot,
      .function = function,
      .class_code = class_register >> 24,
      .subclass = class_register >> 16,
      .programming_interface = class_register >> 8,
      .header_type = header_type & PCI_HEADER_LAYOUT_MASK,
  };
  return true;
}

static bool pci_scan_bus(uint8_t bus);

static bool pci_scan_function(uint8_t bus, uint8_t slot, uint8_t function,
                              uint32_t identity, uint8_t *header_type) {
  uint32_t class_register = pci_config_read(bus, slot, function, 0x08);
  uint8_t header = pci_config_read(bus, slot, function, 0x0e);
  if (header_type != NULL) {
    *header_type = header;
  }
  if (!pci_registry_append(bus, slot, function, identity, class_register,
                           header)) {
    return false;
  }

  uint8_t class_code = class_register >> 24;
  uint8_t subclass = class_register >> 16;
  if (class_code == PCI_CLASS_BRIDGE &&
      (subclass == PCI_SUBCLASS_PCI_BRIDGE ||
       subclass == PCI_SUBCLASS_SEMITRANSPARENT_BRIDGE)) {
    uint8_t secondary_bus =
        pci_config_read(bus, slot, function, 0x18) >> 8;
    if (secondary_bus != bus && !pci_scan_bus(secondary_bus)) {
      return false;
    }
  }
  return true;
}

static bool pci_scan_bus(uint8_t bus) {
  if (pci_registry.visited_buses[bus]) {
    return true;
  }
  pci_registry.visited_buses[bus] = true;

  for (uint8_t slot = 0; slot < PCI_SLOT_COUNT; slot++) {
    uint32_t identity = pci_config_read(bus, slot, 0, 0);
    if ((uint16_t)identity == 0xffffu) {
      continue;
    }

    uint8_t header_type;
    if (!pci_scan_function(bus, slot, 0, identity, &header_type)) {
      return false;
    }
    if ((header_type & PCI_HEADER_MULTIFUNCTION) == 0) {
      continue;
    }
    for (uint8_t function = 1; function < PCI_FUNCTION_COUNT; function++) {
      identity = pci_config_read(bus, slot, function, 0);
      if ((uint16_t)identity != 0xffffu &&
          !pci_scan_function(bus, slot, function, identity, NULL)) {
        return false;
      }
    }
  }
  return true;
}

bool pci_initialize(void) {
  if (pci_registry.initialized) {
    return true;
  }

  uint32_t host_identity = pci_config_read(0, 0, 0, 0);
  uint8_t host_header = pci_config_read(0, 0, 0, 0x0e);
  bool success = true;
  if ((uint16_t)host_identity == 0xffffu ||
      (host_header & PCI_HEADER_MULTIFUNCTION) == 0) {
    success = pci_scan_bus(0);
  } else {
    for (uint8_t function = 0;
         function < PCI_FUNCTION_COUNT && success; function++) {
      if ((uint16_t)pci_config_read(0, 0, function, 0) != 0xffffu) {
        success = pci_scan_bus(function);
      }
    }
  }

  if (!success) {
    free(pci_registry.devices);
    memset(&pci_registry, 0, sizeof(pci_registry));
    return false;
  }
  pci_registry.initialized = true;
  logk("pci: discovered %d function(s)\n", (int)pci_registry.count);
  return true;
}

const pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
  for (size_t index = 0; index < pci_registry.count; index++) {
    const pci_device_t *device = &pci_registry.devices[index];
    if (device->vendor_id == vendor_id && device->device_id == device_id) {
      return device;
    }
  }
  return NULL;
}

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
  for (size_t index = 0; index < pci_registry.count; index++) {
    const pci_device_t *device = &pci_registry.devices[index];
    if (device->class_code == class_code && device->subclass == subclass) {
      return device;
    }
  }
  return NULL;
}

static uint8_t pci_bar_count(const pci_device_t *device) {
  static const uint8_t counts[] = {6, 2, 1};
  return device->header_type < sizeof(counts) ? counts[device->header_type] : 0;
}

bool pci_read_bar(const pci_device_t *device, uint8_t index, pci_bar_t *bar) {
  if (device == NULL || bar == NULL) {
    return false;
  }
  uint8_t count = pci_bar_count(device);
  if (index >= count) {
    return false;
  }

  uint32_t value = pci_config_read(device->bus, device->slot,
                                   device->function, 0x10 + index * 4);
  if (value == 0 || value == UINT_MAX) {
    return false;
  }
  if (value & 1u) {
    *bar = (pci_bar_t){
        .type = PCI_BAR_IO,
        .address = value & ~3u,
        .prefetchable = false,
    };
    return bar->address != 0;
  }

  uint8_t memory_type = (value >> 1) & 3u;
  if (memory_type == 3 || (memory_type == 2 && index + 1 >= count)) {
    return false;
  }
  uint64_t address = value & ~0x0fu;
  if (memory_type == 2) {
    address |= (uint64_t)pci_config_read(device->bus, device->slot,
                                         device->function,
                                         0x10 + (index + 1) * 4)
               << 32;
  }
  *bar = (pci_bar_t){
      .type = memory_type == 2 ? PCI_BAR_MEMORY64 : PCI_BAR_MEMORY32,
      .address = address,
      .prefetchable = (value & 8u) != 0,
  };
  return address != 0;
}

bool pci_find_io_bar(const pci_device_t *device, uint32_t *address) {
  if (device == NULL || address == NULL) {
    return false;
  }
  uint8_t count = pci_bar_count(device);
  for (uint8_t index = 0; index < count; index++) {
    pci_bar_t bar;
    if (pci_read_bar(device, index, &bar) && bar.type == PCI_BAR_IO &&
        bar.address <= UINT_MAX) {
      *address = bar.address;
      return true;
    }
  }
  return false;
}

uint8_t pci_interrupt_line(const pci_device_t *device) {
  return device == NULL
             ? 0xff
             : pci_config_read(device->bus, device->slot, device->function,
                               0x3c);
}

void pci_command_enable(const pci_device_t *device, uint16_t flags) {
  if (device == NULL) {
    return;
  }
  uint16_t command = pci_config_read(device->bus, device->slot,
                                     device->function, 0x04);
  uint32_t address = 0x80000000u | ((uint32_t)device->bus << 16) |
                     ((uint32_t)device->slot << 11) |
                     ((uint32_t)device->function << 8) | 0x04u;
  x86_port_write32(PCI_CONFIG_ADDRESS_PORT, address);
  x86_port_write16(PCI_CONFIG_DATA_PORT, command | flags);
}
