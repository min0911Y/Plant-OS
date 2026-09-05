#include <arch/x86/io.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <pci.h>
#include <platform/pc.h>
#include <stdint.h>

enum {
  PCI_CONFIG_ADDRESS_PORT = 0x0cf8,
  PCI_CONFIG_DATA_PORT = 0x0cfc,
  PCI_BUS_COUNT = 256,
  PCI_SLOT_COUNT = 32,
  PCI_FUNCTION_COUNT = 8,
  PCI_INITIAL_CAPACITY = 16,
  PCI_HEADER_MULTIFUNCTION = 0x80,
  PCI_HEADER_LAYOUT_MASK = 0x7f,
  PCI_CLASS_BRIDGE = 0x06,
  PCI_SUBCLASS_PCI_BRIDGE = 0x04,
  PCI_SUBCLASS_SEMITRANSPARENT_BRIDGE = 0x09,
  PCI_CAP_MSI = 0x05,
  PCI_CAP_MSIX = 0x11,
  PCI_MSI_ENABLE = 1u << 0,
  PCI_MSI_MULTIPLE_ENABLE = 7u << 4,
  PCI_MSI_64BIT = 1u << 7,
  PCI_MSI_MASKABLE = 1u << 8,
  PCI_MSIX_MASK = 1u << 14,
  PCI_MSIX_ENABLE = 1u << 15,
  PCI_ECAM_BUS_SIZE = 1u << 20,
  PCI_CRS_TIMEOUT_MS = 1000,
};

typedef struct {
  uint64_t address;
  uint16_t segment;
  uint8_t first_bus, last_bus;
  uint32_t reserved;
} __attribute__((packed)) pci_mcfg_entry_t;

typedef struct {
  struct ACPISDTHeader header;
  uint64_t reserved;
  pci_mcfg_entry_t entries[];
} __attribute__((packed)) pci_mcfg_t;

typedef struct {
  uint64_t address;
  uint16_t segment;
  uint8_t first_bus, last_bus;
  volatile uint8_t *buses[PCI_BUS_COUNT];
} pci_ecam_t;

_Static_assert(sizeof(pci_mcfg_entry_t) == 16, "MCFG allocation layout");
_Static_assert(sizeof(pci_mcfg_t) == 44, "MCFG header layout");

#ifdef KERNEL_USB_DEBUG
#define pci_log(...) do { logk(__VA_ARGS__); printk(__VA_ARGS__); } while (0)
#else
#define pci_log(...) logk(__VA_ARGS__)
#endif

typedef struct {
  pci_device_t *devices;
  size_t count;
  size_t capacity;
  pci_ecam_t *ecam;
  size_t ecam_count;
  bool legacy_scanned[PCI_BUS_COUNT];
  bool initialized;
} pci_registry_t;

static pci_registry_t pci_registry;

static pci_ecam_t *pci_ecam_for_bus(uint16_t segment, uint8_t bus) {
  for (size_t i = 0; i < pci_registry.ecam_count; i++) {
    pci_ecam_t *ecam = &pci_registry.ecam[i];
    if (ecam->segment == segment && bus >= ecam->first_bus && bus <= ecam->last_bus) {
      return ecam;
    }
  }
  return NULL;
}

static bool pci_configuration_initialize(void) {
  const pci_mcfg_t *mcfg = acpi_find_table("MCFG");
  if (mcfg == NULL) {
    pci_log("pci: MCFG absent; configuration access=CF8/CFC\n");
    return true;
  }
  if (mcfg->header.Length < sizeof(*mcfg) ||
      (mcfg->header.Length - sizeof(*mcfg)) % sizeof(*mcfg->entries)) {
    pci_log("pci: malformed MCFG length\n");
    return false;
  }
  size_t count = (mcfg->header.Length - sizeof(*mcfg)) / sizeof(*mcfg->entries);
  if (count == 0 || count > SIZE_MAX / sizeof(pci_ecam_t)) {
    return false;
  }
  pci_ecam_t *windows = malloc(count * sizeof(*windows));
  if (windows == NULL) {
    return false;
  }
  memset(windows, 0, count * sizeof(*windows));
  for (size_t i = 0; i < count; i++) {
    const pci_mcfg_entry_t *entry = &mcfg->entries[i];
    uint64_t extent = ((uint64_t)entry->last_bus + 1) * PCI_ECAM_BUS_SIZE;
    bool valid = entry->address != 0 && !(entry->address & (PCI_ECAM_BUS_SIZE - 1)) &&
                 entry->first_bus <= entry->last_bus && entry->address <= UINT64_MAX - extent;
    for (size_t j = 0; valid && j < i; j++) {
      valid = windows[j].segment != entry->segment ||
              entry->last_bus < windows[j].first_bus || entry->first_bus > windows[j].last_bus;
    }
    if (!valid) {
      pci_log("pci: invalid/overlapping MCFG allocation %u\n", (unsigned)i);
      free(windows);
      return false;
    }
    windows[i].address = entry->address;
    windows[i].segment = entry->segment;
    windows[i].first_bus = entry->first_bus;
    windows[i].last_bus = entry->last_bus;
    pci_log("pci: ECAM segment=%04x buses=%02x-%02x base=%llx\n",
            entry->segment, entry->first_bus, entry->last_bus,
            (unsigned long long)entry->address);
  }
  pci_registry.ecam = windows;
  pci_registry.ecam_count = count;
  return true;
}

static uint32_t pci_config_read(uint16_t segment, uint8_t bus, uint8_t slot,
                                uint8_t function, uint16_t offset) {
  if (slot >= PCI_SLOT_COUNT || function >= PCI_FUNCTION_COUNT) {
    return UINT_MAX;
  }
  pci_ecam_t *ecam = pci_ecam_for_bus(segment, bus);
  if (ecam != NULL) {
    if (ecam->buses[bus] == NULL || offset >= 4096) {
      return UINT_MAX;
    }
    volatile uint32_t *reg = (volatile uint32_t *)(ecam->buses[bus] +
        ((unsigned)slot << 15) + ((unsigned)function << 12) + (offset & ~3u));
    return *reg >> ((offset & 3u) * 8u);
  }
  if (segment != 0 || offset >= 256) {
    return UINT_MAX;
  }
  uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                     ((uint32_t)slot << 11) | ((uint32_t)function << 8) |
                     (offset & 0xfcu);
  irq_state_t state = irq_save();
  x86_port_write32(PCI_CONFIG_ADDRESS_PORT, address);
  uint32_t value = x86_port_read32(PCI_CONFIG_DATA_PORT);
  irq_restore(state);
  return value >> ((offset & 3u) * 8u);
}

static void pci_config_write(const pci_device_t *device, uint16_t offset,
                             uint32_t value, unsigned width) {
  pci_ecam_t *ecam = pci_ecam_for_bus(device->segment, device->bus);
  if (device->slot >= PCI_SLOT_COUNT || device->function >= PCI_FUNCTION_COUNT ||
      (width != 2 && width != 4) || (offset & (width - 1))) {
    return;
  }
  if (ecam != NULL) {
    if (ecam->buses[device->bus] == NULL || offset > 4096 - width) {
      return;
    }
    volatile uint8_t *reg = ecam->buses[device->bus] +
        ((unsigned)device->slot << 15) + ((unsigned)device->function << 12) + offset;
    if (width == 2) {
      *(volatile uint16_t *)reg = value;
    } else {
      *(volatile uint32_t *)reg = value;
    }
    return;
  }
  if (device->segment != 0 || offset > 256 - width) {
    return;
  }
  uint32_t address = 0x80000000u | ((uint32_t)device->bus << 16) |
                     ((uint32_t)device->slot << 11) |
                     ((uint32_t)device->function << 8) | (offset & 0xfcu);
  irq_state_t state = irq_save();
  x86_port_write32(PCI_CONFIG_ADDRESS_PORT, address);
  if (width == 2) {
    /* Never write the adjacent, write-one-to-clear PCI status register. */
    x86_port_write16(PCI_CONFIG_DATA_PORT + (offset & 2u), value);
  } else {
    x86_port_write32(PCI_CONFIG_DATA_PORT, value);
  }
  irq_restore(state);
}

static bool pci_registry_append(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function,
                                uint32_t identity, uint32_t class_register,
                                uint8_t header_type) {
  if (pci_registry.count == pci_registry.capacity) {
    size_t limit = SIZE_MAX / sizeof(*pci_registry.devices);
    if (pci_registry.capacity == limit) {
      return false;
    }
    size_t capacity = pci_registry.capacity == 0 ? PCI_INITIAL_CAPACITY
                      : pci_registry.capacity > limit / 2 ? limit
                                                          : pci_registry.capacity * 2;
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
      .segment = segment,
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

static bool pci_scan_bus(uint16_t segment, uint8_t bus);

static uint32_t pci_probe_identity(uint16_t segment, uint8_t bus, uint8_t slot,
                                    uint8_t function) {
  uint32_t identity = pci_config_read(segment, bus, slot, function, 0);
  if ((uint16_t)identity == 1) {
    /* PCIe CRS Software Visibility reports vendor 0001 while the function
     * is not ready. Do not cache it as a device or read its class too early. */
    uint64_t deadline = monotonic_time_ns() + PCI_CRS_TIMEOUT_MS * 1000000ull;
    do {
      sleep(1);
      identity = pci_config_read(segment, bus, slot, function, 0);
    } while ((uint16_t)identity == 1 && monotonic_time_ns() < deadline);
    if ((uint16_t)identity == 1) {
      pci_log("pci: %04x:%02x:%02x.%u CRS readiness timeout\n",
              segment, bus, slot, function);
      return UINT_MAX;
    }
  }
  return (uint16_t)identity == 0 ? UINT_MAX : identity;
}

static bool pci_scan_function(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t function,
                              uint32_t identity, uint8_t *header_type) {
  uint32_t class_register = pci_config_read(segment, bus, slot, function, 0x08);
  uint8_t header = pci_config_read(segment, bus, slot, function, 0x0e);
  if (header_type != NULL) {
    *header_type = header;
  }
  if (!pci_registry_append(segment, bus, slot, function, identity, class_register,
                           header)) {
    return false;
  }

  uint8_t class_code = class_register >> 24;
  uint8_t subclass = class_register >> 16;
  if (class_code == PCI_CLASS_BRIDGE &&
      (subclass == PCI_SUBCLASS_PCI_BRIDGE ||
       subclass == PCI_SUBCLASS_SEMITRANSPARENT_BRIDGE)) {
    uint32_t buses = pci_config_read(segment, bus, slot, function, 0x18);
    uint8_t secondary = buses >> 8, subordinate = buses >> 16;
    if (secondary != 0 && secondary != bus && secondary <= subordinate &&
        !pci_scan_bus(segment, secondary)) {
      return false;
    }
  }
  return true;
}

static bool pci_scan_bus(uint16_t segment, uint8_t bus) {
  pci_ecam_t *ecam = pci_ecam_for_bus(segment, bus);
  if (ecam == NULL && segment != 0) {
    pci_log("pci: segment=%04x bus=%02x is outside MCFG\n", segment, bus);
    return false;
  }
  if (ecam == NULL ? pci_registry.legacy_scanned[bus] : ecam->buses[bus] != NULL) {
    return true;
  }
  if (ecam != NULL) {
    /* MCFG addresses are relative to bus zero, even for a nonzero StartBus. */
    uint64_t physical = ecam->address + (uint64_t)bus * PCI_ECAM_BUS_SIZE;
    ecam->buses[bus] = arch_mmio_map(physical, PCI_ECAM_BUS_SIZE);
    if (ecam->buses[bus] == NULL) {
      pci_log("pci: ECAM mapping failed segment=%04x bus=%02x\n", segment, bus);
      return false;
    }
  } else {
    pci_registry.legacy_scanned[bus] = true;
  }
  size_t first_device = pci_registry.count;

  for (uint8_t slot = 0; slot < PCI_SLOT_COUNT; slot++) {
    uint32_t identity = pci_probe_identity(segment, bus, slot, 0);
    if ((uint16_t)identity == 0xffffu) {
      continue;
    }

    uint8_t header_type;
    if (!pci_scan_function(segment, bus, slot, 0, identity, &header_type)) {
      return false;
    }
    if ((header_type & PCI_HEADER_MULTIFUNCTION) == 0) {
      continue;
    }
    for (uint8_t function = 1; function < PCI_FUNCTION_COUNT; function++) {
      identity = pci_probe_identity(segment, bus, slot, function);
      if ((uint16_t)identity != 0xffffu &&
          !pci_scan_function(segment, bus, slot, function, identity, NULL)) {
        return false;
      }
    }
  }
  if (pci_registry.count != first_device) {
    pci_log("pci: segment=%04x bus=%02x access=%s functions=%u (including bridges)\n",
            segment, bus, ecam == NULL ? "CF8" : "ECAM",
            (unsigned)(pci_registry.count - first_device));
  }
  return true;
}

bool pci_initialize(void) {
  if (pci_registry.initialized) {
    return true;
  }

  bool success = pci_configuration_initialize() && pci_scan_bus(0, 0);
  for (size_t i = 0; success && i < pci_registry.ecam_count; i++) {
    pci_ecam_t *ecam = &pci_registry.ecam[i];
    /* MCFG describes address coverage, not a root-bridge list. Independent
     * roots may be anywhere in that coverage and need not be reachable from
     * bus zero. Already visited buses return immediately; absent slots still
     * cost only one vendor read, with functions 1..7 gated by multifunction. */
    for (unsigned bus = ecam->first_bus; success && bus <= ecam->last_bus; bus++) {
      size_t previous = pci_registry.count;
      success = pci_scan_bus(ecam->segment, bus);
      if (success && pci_registry.count != previous) {
        pci_log("pci: ECAM discovery entry segment=%04x bus=%02x added=%u\n",
                ecam->segment, bus, (unsigned)(pci_registry.count - previous));
      }
    }
  }

  if (!success) {
    free(pci_registry.devices);
    free(pci_registry.ecam);
    memset(&pci_registry, 0, sizeof(pci_registry));
    return false;
  }
  pci_registry.initialized = true;
  pci_log("pci: discovered %d function(s)\n", (int)pci_registry.count);
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

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass,
                                   const pci_device_t *after) {
  size_t start = after == NULL ? 0 : (size_t)(after - pci_registry.devices) + 1;
  for (size_t index = start; index < pci_registry.count; index++) {
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

  for (uint8_t previous = 0; previous < index; previous++) {
    uint32_t value = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, 0x10 + previous * 4);
    if ((value & 7u) == 4u && ++previous == index) {
      return false; /* The upper half of a 64-bit BAR is not another BAR. */
    }
  }

  uint32_t value = pci_config_read(device->segment, device->bus, device->slot, device->function,
                                   0x10 + index * 4);
  if (value == 0 || value == UINT_MAX) {
    return false;
  }
  bool io = (value & 1u) != 0;
  uint8_t memory_type = (value >> 1) & 3u;
  bool wide = !io && memory_type == 2;
  if (!io && (memory_type == 3 || (wide && index + 1 >= count))) {
    return false;
  }
  uint8_t offset = 0x10 + index * 4;
  uint32_t upper = wide ? pci_config_read(device->segment, device->bus, device->slot,
                                          device->function, offset + 4)
                        : 0;
  irq_state_t state = irq_save();
  uint16_t command =
      pci_config_read(device->segment, device->bus, device->slot, device->function, 0x04);
  pci_config_write(device, 0x04,
                   command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                               PCI_COMMAND_BUS_MASTER), 2);
  pci_config_write(device, offset, UINT_MAX, 4);
  if (wide) {
    pci_config_write(device, offset + 4, UINT_MAX, 4);
  }
  uint64_t mask =
      pci_config_read(device->segment, device->bus, device->slot, device->function, offset) &
      (io ? ~3u : ~15u);
  if (wide) {
    mask |= (uint64_t)pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, offset + 4)
            << 32;
    pci_config_write(device, offset + 4, upper, 4);
  }
  pci_config_write(device, offset, value, 4);
  pci_config_write(device, 0x04, command, 2);
  irq_restore(state);
  /* Unimplemented upper address bits may read as zero on 16-bit I/O
   * decoders and the legacy below-1-MiB memory BAR type. */
  if (io) {
    mask |= 0xffff0000u;
  } else if (memory_type == 1) {
    mask |= 0xfff00000u;
  }
  uint64_t size = wide ? ~mask + 1 : (uint32_t)(~mask + 1);
  if (mask == 0 || size == 0 || (size & (size - 1)) != 0) {
    return false;
  }
  if (io) {
    *bar = (pci_bar_t){
        .type = PCI_BAR_IO,
        .address = value & ~3u,
        .size = size,
        .prefetchable = false,
    };
    return bar->address != 0;
  }

  uint64_t address = value & ~0x0fu;
  address |= (uint64_t)upper << 32;
  *bar = (pci_bar_t){
      .type = memory_type == 2 ? PCI_BAR_MEMORY64 : PCI_BAR_MEMORY32,
      .address = address,
      .size = size,
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
    if (!pci_read_bar(device, index, &bar)) {
      continue;
    }
    if (bar.type == PCI_BAR_IO && bar.address <= UINT_MAX) {
      *address = bar.address;
      return true;
    }
    if (bar.type == PCI_BAR_MEMORY64) {
      index++;
    }
  }
  return false;
}

uint8_t pci_interrupt_line(const pci_device_t *device) {
  return device == NULL ? 0xff
                        : pci_config_read(device->segment, device->bus, device->slot,
                                          device->function, 0x3c);
}

void pci_command_update(const pci_device_t *device, uint16_t enable,
                        uint16_t disable) {
  if (device == NULL) {
    return;
  }
  irq_state_t state = irq_save();
  uint16_t command =
      pci_config_read(device->segment, device->bus, device->slot, device->function, 0x04);
  pci_config_write(device, 0x04, (command | enable) & ~disable, 2);
  irq_restore(state);
}

static bool pci_irq_capabilities(const pci_device_t *device, uint8_t *msi,
                                  uint8_t *msix) {
  *msi = *msix = 0;
  if (!(pci_config_read(device->segment, device->bus, device->slot, device->function, 0x06) &
        (1u << 4))) {
    return true;
  }
  if (device->header_type > 1) {
    return false;
  }
  uint64_t visited = 0;
  uint8_t offset = pci_config_read(device->segment, device->bus, device->slot,
                                   device->function, 0x34);
  while (offset != 0) {
    if (offset < 0x40 || (offset & 3) || (visited & (1ull << (offset / 4)))) {
      return false;
    }
    visited |= 1ull << (offset / 4);
    uint32_t header = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, offset);
    if ((header & 255) == PCI_CAP_MSI) {
      if (*msi != 0) {
        return false;
      }
      *msi = offset;
    } else if ((header & 255) == PCI_CAP_MSIX) {
      if (*msix != 0) {
        return false;
      }
      *msix = offset;
    }
    offset = header >> 8;
  }
  return true;
}

static bool pci_msi_enable(const pci_device_t *device, uint8_t capability,
                           const irq_message_t *message) {
  uint16_t control = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, capability + 2);
  bool wide = (control & PCI_MSI_64BIT) != 0;
  unsigned data_offset = capability + (wide ? 12 : 8);
  unsigned mask_offset = data_offset + 4;
  if (data_offset + 2 > 256 || (!wide && message->address > UINT_MAX) ||
      ((control & PCI_MSI_MASKABLE) && mask_offset + 8 > 256)) {
    return false;
  }
  control &= ~(PCI_MSI_ENABLE | PCI_MSI_MULTIPLE_ENABLE);
  pci_config_write(device, capability + 2, control, 2);
  uint32_t mask = 0;
  if (control & PCI_MSI_MASKABLE) {
    mask = pci_config_read(device->segment, device->bus, device->slot, device->function,
                            mask_offset);
    pci_config_write(device, mask_offset, mask | 1, 4);
  }
  pci_config_write(device, capability + 4, message->address, 4);
  if (wide) {
    pci_config_write(device, capability + 8, message->address >> 32, 4);
  }
  pci_config_write(device, data_offset, message->data, 2);
  pci_config_write(device, capability + 2, control | PCI_MSI_ENABLE, 2);
  uint16_t enabled = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, capability + 2);
  if ((enabled & (PCI_MSI_ENABLE | PCI_MSI_MULTIPLE_ENABLE)) != PCI_MSI_ENABLE) {
    pci_config_write(device, capability + 2, control, 2);
    return false;
  }
  if (control & PCI_MSI_MASKABLE) {
    pci_config_write(device, mask_offset, mask & ~1u, 4);
  }
  return true;
}

static volatile uint32_t *pci_msix_enable(const pci_device_t *device,
                                         uint8_t capability,
                                         const irq_message_t *message) {
  if ((unsigned)capability + 12 > 256) {
    return NULL;
  }
  uint16_t control = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, capability + 2);
  uint32_t location = pci_config_read(device->segment, device->bus, device->slot,
                                       device->function, capability + 4);
  unsigned count = (control & 0x7ff) + 1;
  uint64_t offset = location & ~7u;
  pci_bar_t bar;
  if (!pci_read_bar(device, location & 7, &bar) || bar.type == PCI_BAR_IO ||
      offset > bar.size || count * 16u > bar.size - offset ||
      offset > UINT64_MAX - bar.address ||
      count * 16u > UINT64_MAX - (bar.address + offset)) {
    return NULL;
  }
  volatile uint32_t *table = arch_mmio_map(bar.address + offset, count * 16u);
  if (table == NULL) {
    return NULL;
  }
  pci_command_update(device, PCI_COMMAND_MEMORY, 0);
  /* Function masking protects every table entry, including ones left by
   * firmware. Only entry zero will be exposed to this binding. */
  control = (control | PCI_MSIX_MASK) & ~PCI_MSIX_ENABLE;
  pci_config_write(device, capability + 2, control, 2);
  for (unsigned i = 0; i < count; i++) {
    table[i * 4 + 3] |= 1u;
  }
  table[0] = message->address;
  table[1] = message->address >> 32;
  table[2] = message->data;
  (void)table[3]; /* Flush the posted MMIO writes before enabling delivery. */
  pci_config_write(device, capability + 2,
                    (control | PCI_MSIX_ENABLE) & ~PCI_MSIX_MASK, 2);
  uint16_t enabled = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, capability + 2);
  if ((enabled & (PCI_MSIX_ENABLE | PCI_MSIX_MASK)) != PCI_MSIX_ENABLE) {
    pci_config_write(device, capability + 2, control, 2);
    return NULL;
  }
  table[3] &= ~1u;
  (void)table[3];
  return table;
}

bool pci_irq_initialize(const pci_device_t *device, irq_handler_t handler,
                         pci_irq_t *interrupt) {
  if (device == NULL || handler == NULL || interrupt == NULL ||
      interrupt->mode != PCI_IRQ_NONE) {
    return false;
  }
  uint8_t msi, msix;
  if (!pci_irq_capabilities(device, &msi, &msix)) {
    return false;
  }
  /* The caller has stopped its device. Disable all old transports before
   * preparing a handler, so an enabled firmware capability cannot steal INTx. */
  pci_command_update(device, PCI_COMMAND_INTX_DISABLE, 0);
  if (msi != 0) {
    uint16_t control = pci_config_read(device->segment, device->bus, device->slot,
                                        device->function, msi + 2);
    pci_config_write(device, msi + 2, control & ~PCI_MSI_ENABLE, 2);
  }
  if (msix != 0) {
    uint16_t control = pci_config_read(device->segment, device->bus, device->slot,
                                        device->function, msix + 2);
    pci_config_write(device, msix + 2,
                      (control | PCI_MSIX_MASK) & ~PCI_MSIX_ENABLE, 2);
  }
  irq_message_t message;
  if ((msi || msix) && irq_allocate_message(handler, &message)) {
    volatile uint32_t *table = msix ? pci_msix_enable(device, msix, &message) : NULL;
    if (table != NULL) {
      *interrupt = (pci_irq_t){.device = device, .handler = handler,
                                .table = table, .irq = message.irq,
                                .capability = msix, .mode = PCI_IRQ_MSIX};
      return true;
    }
    if (msi && pci_msi_enable(device, msi, &message)) {
      *interrupt = (pci_irq_t){.device = device, .handler = handler,
                                .irq = message.irq, .capability = msi,
                                .mode = PCI_IRQ_MSI};
      return true;
    }
    irq_unregister_handler(message.irq, handler);
  }
  uint32_t routing = pci_config_read(device->segment, device->bus, device->slot,
                                      device->function, 0x3c);
  unsigned irq = routing & 255, pin = (routing >> 8) & 255;
  if (irq == 0 || !irq_is_valid(irq) || pin == 0 || pin > 4 ||
      !irq_register_handler(irq, handler, IRQ_SHARED)) {
    return false;
  }
  *interrupt = (pci_irq_t){.device = device, .handler = handler,
                            .irq = irq, .mode = PCI_IRQ_INTX};
  irq_configure(irq, IRQ_TRIGGER_LEVEL, IRQ_POLARITY_LOW);
  pci_command_update(device, 0, PCI_COMMAND_INTX_DISABLE);
  irq_mask_clear(irq);
  return true;
}

void pci_irq_release(pci_irq_t *interrupt) {
  if (interrupt == NULL || interrupt->mode == PCI_IRQ_NONE) {
    return;
  }
  const pci_device_t *device = interrupt->device;
  pci_command_update(device, PCI_COMMAND_INTX_DISABLE, 0);
  if (interrupt->mode != PCI_IRQ_INTX) {
    unsigned offset = interrupt->capability + 2;
    uint16_t control = pci_config_read(device->segment, device->bus, device->slot,
                                        device->function, offset);
    if (interrupt->mode == PCI_IRQ_MSIX) {
      interrupt->table[3] |= 1u;
      (void)interrupt->table[3];
      control = (control | PCI_MSIX_MASK) & ~PCI_MSIX_ENABLE;
    } else {
      control &= ~PCI_MSI_ENABLE;
    }
    pci_config_write(device, offset, control, 2);
    (void)pci_config_read(device->segment, device->bus, device->slot, device->function, offset);
  }
  irq_unregister_handler(interrupt->irq, interrupt->handler);
  *interrupt = (pci_irq_t){0};
}
