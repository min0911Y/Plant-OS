#ifndef PLANT_OS_PCI_H
#define PLANT_OS_PCI_H

#include <ctypes.h>

typedef struct {
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t bus;
  uint8_t slot;
  uint8_t function;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t programming_interface;
  uint8_t header_type;
} pci_device_t;

typedef enum {
  PCI_BAR_IO,
  PCI_BAR_MEMORY32,
  PCI_BAR_MEMORY64,
} pci_bar_type_t;

typedef struct {
  pci_bar_type_t type;
  uint64_t address;
  bool prefetchable;
} pci_bar_t;

enum {
  PCI_COMMAND_IO = 1u << 0,
  PCI_COMMAND_MEMORY = 1u << 1,
  PCI_COMMAND_BUS_MASTER = 1u << 2,
};

bool pci_initialize(void);
const pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
bool pci_read_bar(const pci_device_t *device, uint8_t index, pci_bar_t *bar);
bool pci_find_io_bar(const pci_device_t *device, uint32_t *address);
uint8_t pci_interrupt_line(const pci_device_t *device);
void pci_command_enable(const pci_device_t *device, uint16_t flags);

#endif
