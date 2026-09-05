#ifndef PLANT_OS_PCI_H
#define PLANT_OS_PCI_H

#include <ctypes.h>
#include <interrupts.h>

typedef struct {
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t segment;
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
  uint64_t size;
  bool prefetchable;
} pci_bar_t;

typedef enum {
  PCI_IRQ_NONE,
  PCI_IRQ_INTX,
  PCI_IRQ_MSI,
  PCI_IRQ_MSIX,
} pci_irq_mode_t;

typedef struct {
  const pci_device_t *device;
  irq_handler_t handler;
  volatile uint32_t *table;
  unsigned irq;
  uint8_t capability;
  pci_irq_mode_t mode;
} pci_irq_t;

enum {
  PCI_COMMAND_IO = 1u << 0,
  PCI_COMMAND_MEMORY = 1u << 1,
  PCI_COMMAND_BUS_MASTER = 1u << 2,
  PCI_COMMAND_INTX_DISABLE = 1u << 10,
};

bool pci_initialize(void);
const pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass,
                                   const pci_device_t *after);
bool pci_read_bar(const pci_device_t *device, uint8_t index, pci_bar_t *bar);
bool pci_find_io_bar(const pci_device_t *device, uint32_t *address);
uint8_t pci_interrupt_line(const pci_device_t *device);
void pci_command_update(const pci_device_t *device, uint16_t enable,
                        uint16_t disable);

/* The device must be quiescent. Select one primary MSI-X/MSI message, or
 * shared INTx. Initialize the binding to zero; stop the device before release. */
bool pci_irq_initialize(const pci_device_t *device, irq_handler_t handler,
                         pci_irq_t *interrupt);
void pci_irq_release(pci_irq_t *interrupt);

#endif
