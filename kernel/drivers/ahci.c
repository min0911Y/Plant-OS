#include <dma.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <pci.h>
#include <stdint.h>

enum {
  AHCI_PORTS = 32,
  AHCI_CONTROL_BYTES = 4096,
  AHCI_BUFFER_BYTES = 65536,
  AHCI_FIS_OFFSET = 1024,
  AHCI_TABLE_OFFSET = 1280,
  AHCI_SETUP_MS = 1000,
  AHCI_IO_MS = 5000,
  AHCI_GHC_RESET = 1u << 0,
  AHCI_GHC_IRQ = 1u << 1,
  AHCI_GHC_ENABLE = 1u << 31,
  AHCI_CMD_START = 1u << 0,
  AHCI_CMD_SPINUP = 1u << 1,
  AHCI_CMD_POWER = 1u << 2,
  AHCI_CMD_FIS = 1u << 4,
  AHCI_CMD_FIS_RUNNING = 1u << 14,
  AHCI_CMD_RUNNING = 1u << 15,
  AHCI_TFD_BUSY = 0x80,
  AHCI_TFD_DRQ = 0x08,
  AHCI_TFD_ERROR = 0x21, /* ATA ERR and device-fault status bits. */
  AHCI_IRQ_ERROR = (1u << 30) | (1u << 29) | (1u << 28) | (1u << 27) | (1u << 24),
  AHCI_IRQ_COMPLETE = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5),
  AHCI_ATA_SIGNATURE = 0x00000101,
  AHCI_ATAPI_SIGNATURE = 0xeb140101,
  AHCI_IDENTIFY = 0xec,
  AHCI_READ = 0xc8,
  AHCI_WRITE = 0xca,
  AHCI_READ_EXT = 0x25,
  AHCI_WRITE_EXT = 0x35,
  AHCI_FLUSH = 0xe7,
  AHCI_FLUSH_EXT = 0xea,
};

typedef struct {
  uint32_t clb, clbu, fb, fbu, is, ie, cmd, reserved0;
  uint32_t tfd, sig, ssts, sctl, serr, sact, ci, sntf, fbs;
  uint32_t reserved1[11], vendor[4];
} ahci_port_regs_t;

typedef struct {
  uint32_t cap, ghc, is, pi, version, ccc_ctl, ccc_ports;
  uint32_t em_location, em_control, cap2, bohc;
  uint8_t reserved[0x100 - 0x2c];
  ahci_port_regs_t ports[AHCI_PORTS];
} ahci_regs_t;

typedef struct {
  uint16_t flags, prdt_length;
  volatile uint32_t transferred;
  uint64_t table;
  uint32_t reserved[4];
} ahci_command_header_t;

typedef struct {
  uint8_t type, flags, command, features;
  uint8_t lba0, lba1, lba2, device;
  uint8_t lba3, lba4, lba5, features_high;
  uint16_t count;
  uint8_t icc, control, reserved[4];
} __attribute__((packed)) ahci_fis_t;

typedef struct {
  uint8_t fis[64], atapi[16], reserved[48];
  struct {
    uint64_t address;
    uint32_t reserved, count;
  } prdt;
} ahci_command_table_t;

typedef enum { AHCI_IDLE, AHCI_BUSY, AHCI_DONE, AHCI_FAILED } ahci_state_t;
typedef struct ahci_host ahci_host_t;
typedef struct {
  ahci_host_t *host;
  volatile ahci_port_regs_t *regs;
  void *control, *buffer;
  dma_addr_t control_address, buffer_address;
  uint64_t sectors;
  uint32_t tid, generation, interrupt_status;
  unsigned index;
  ahci_state_t state;
  uint8_t flush_command;
  char drive;
  bool lba48, halted;
} ahci_port_t;

struct ahci_host {
  ahci_host_t *next;
  const pci_device_t *pci;
  volatile ahci_regs_t *regs;
  pci_irq_t interrupt;
  ahci_port_t *ports[AHCI_PORTS];
  uint32_t implemented;
  dma_addr_t dma_limit;
  bool online;
};

static ahci_host_t *ahci_hosts;
static ahci_port_t *ahci_drives[26];

_Static_assert(sizeof(ahci_port_regs_t) == 128, "AHCI port register layout");
_Static_assert(__builtin_offsetof(ahci_regs_t, ports) == 0x100, "AHCI port offset");
_Static_assert(sizeof(ahci_command_header_t) == 32, "AHCI command header layout");
_Static_assert(sizeof(ahci_fis_t) == 20, "AHCI register FIS layout");
_Static_assert(__builtin_offsetof(ahci_command_table_t, prdt) == 128, "AHCI PRDT offset");
_Static_assert(sizeof(ahci_command_table_t) <= 256, "AHCI command table allocation");

#define ahci_log(...) do { logk(__VA_ARGS__); printk(__VA_ARGS__); } while (0)

static bool ahci_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value,
                       unsigned timeout_ms) {
  uint64_t deadline = monotonic_time_ns() + timeout_ms * 1000000ull;
  do {
    uint32_t current = *reg;
    if (current == UINT_MAX) {
      return false;
    }
    if ((current & mask) == value) {
      return true;
    }
    sleep(1);
  } while (monotonic_time_ns() < deadline);
  return false;
}

static void ahci_report(ahci_host_t *host, unsigned index, const char *reason) {
  const pci_device_t *pci = host->pci;
  ahci_log("ahci: %04x:%02x:%02x.%u %s\n", pci->segment, pci->bus,
           pci->slot, pci->function, reason);
  if (index < AHCI_PORTS) {
    volatile ahci_port_regs_t *port = &host->regs->ports[index];
    ahci_log("ahci: port=%u cmd=%08x tfd=%08x ci=%08x ssts=%08x serr=%08x\n",
             index, port->cmd, port->tfd, port->ci, port->ssts, port->serr);
  } else if (host->regs != NULL) {
    ahci_log("ahci: ghc=%08x is=%08x bohc=%08x\n", host->regs->ghc,
             host->regs->is, host->regs->version >= 0x10200 ? host->regs->bohc : 0);
  }
}

static bool ahci_stop_port(ahci_port_t *port) {
  port->regs->ie = 0;
  port->regs->cmd &= ~AHCI_CMD_START;
  if (!ahci_wait(&port->regs->cmd, AHCI_CMD_RUNNING, 0, AHCI_SETUP_MS)) {
    return false;
  }
  /* FRE may only be cleared after the command-list engine is stopped. */
  port->regs->cmd &= ~AHCI_CMD_FIS;
  port->halted = ahci_wait(&port->regs->cmd, AHCI_CMD_FIS_RUNNING, 0, AHCI_SETUP_MS);
  return port->halted;
}

static mtask *ahci_waiter(ahci_port_t *port) {
  mtask *task = get_task(port->tid);
  return task != NULL && task->generation == port->generation &&
         task->state != DIED && !task->terminate_pending ? task : NULL;
}

static void ahci_remove_drive(ahci_port_t *port) {
  if (port->drive >= 'A' && port->drive <= 'Z') {
    char drive = port->drive;
    port->drive = 0;
    ahci_drives[drive - 'A'] = NULL;
    logout_vdisk(drive);
  }
}

static void ahci_fail_port(ahci_port_t *port, const char *reason) {
  ahci_host_t *host = port->host;
  ahci_report(host, port->index, reason);
  port->state = AHCI_FAILED;
  ahci_remove_drive(port);
  if (ahci_stop_port(port)) {
    return;
  }
  /* A stuck engine may still DMA. Block the entire function and retain all
   * DMA allocations whose engines have not acknowledged stop. */
  host->online = false;
  host->regs->ghc &= ~AHCI_GHC_IRQ;
  pci_command_update(host->pci, PCI_COMMAND_INTX_DISABLE, PCI_COMMAND_BUS_MASTER);
  pci_irq_release(&host->interrupt);
  for (unsigned i = 0; i < AHCI_PORTS; i++) {
    ahci_port_t *other = host->ports[i];
    if (other == NULL) {
      continue;
    }
    other->regs->ie = 0;
    mtask *waiter = other->state == AHCI_BUSY ? ahci_waiter(other) : NULL;
    other->state = AHCI_FAILED;
    ahci_remove_drive(other);
    if (waiter != NULL) {
      task_run(waiter);
    }
  }
  ahci_report(host, port->index, "engine stop timeout; bus master disabled, DMA quarantined");
}

static bool ahci_interrupt(unsigned irq) {
  bool reschedule = false;
  for (ahci_host_t *host = ahci_hosts; host != NULL; host = host->next) {
    if (!host->online || host->interrupt.irq != irq) {
      continue;
    }
    uint32_t pending = host->regs->is & host->implemented;
    while (pending != 0) {
      unsigned index = __builtin_ctz(pending);
      pending &= pending - 1;
      volatile ahci_port_regs_t *regs = &host->regs->ports[index];
      uint32_t status = regs->is;
      regs->is = status;
      ahci_port_t *port = host->ports[index];
      if (port != NULL && port->state == AHCI_BUSY &&
          ((status & AHCI_IRQ_ERROR) || !(regs->ci & 1))) {
        port->interrupt_status = status;
        port->state = (status & AHCI_IRQ_ERROR) || (regs->tfd & AHCI_TFD_ERROR)
                          ? AHCI_FAILED : AHCI_DONE;
        mtask *waiter = ahci_waiter(port);
        if (waiter != NULL) {
          task_run(waiter);
          reschedule |= waiter != current_task();
        } else if (port->state == AHCI_DONE) {
          port->state = AHCI_IDLE;
        }
      }
      host->regs->is = 1u << index;
    }
  }
  return reschedule;
}

static bool ahci_execute(ahci_port_t *port, uint8_t command, uint64_t lba,
                          unsigned sectors, unsigned bytes, bool write) {
  if (!port->host->online || port->state != AHCI_IDLE || bytes > AHCI_BUFFER_BYTES) {
    return false;
  }
  if (!ahci_wait(&port->regs->tfd, AHCI_TFD_BUSY | AHCI_TFD_DRQ, 0, AHCI_SETUP_MS)) {
    ahci_fail_port(port, "device readiness timeout");
    return false;
  }
  struct TIMER *timer = timer_alloc();
  if (timer == NULL) {
    return false;
  }
  ahci_command_header_t *header = port->control;
  ahci_command_table_t *table = (void *)((uint8_t *)port->control + AHCI_TABLE_OFFSET);
  memset(header, 0, sizeof(*header));
  memset(table, 0, sizeof(*table));
  header->flags = sizeof(ahci_fis_t) / 4 | (write ? 1u << 6 : 0);
  header->prdt_length = bytes != 0;
  header->table = port->control_address + AHCI_TABLE_OFFSET;
  if (bytes != 0) {
    table->prdt.address = port->buffer_address;
    table->prdt.count = (bytes - 1) | (1u << 31);
  }
  ahci_fis_t *fis = (void *)table->fis;
  *fis = (ahci_fis_t){.type = 0x27, .flags = 0x80, .command = command,
                       .lba0 = lba, .lba1 = lba >> 8, .lba2 = lba >> 16,
                       .device = sectors ? 0x40 : 0, .count = sectors};
  if (port->lba48) {
    fis->lba3 = lba >> 24;
    fis->lba4 = lba >> 32;
    fis->lba5 = lba >> 40;
  } else if (sectors != 0) {
    fis->device |= (lba >> 24) & 15;
  }
  if (write) {
    dma_sync_for_device(port->buffer, bytes);
  }
  dma_sync_for_device(table, sizeof(*table));
  dma_sync_for_device(header, sizeof(*header));

  unsigned char expired;
  struct FIFO8 fifo;
  fifo8_init(&fifo, 1, &expired);
  uint64_t deadline = monotonic_time_ns() + AHCI_IO_MS * 1000000ull;
  irq_state_t saved = irq_save();
  mtask *task = current_task();
  port->tid = task->tid;
  port->generation = task->generation;
  port->state = AHCI_BUSY;
  port->interrupt_status = 0;
  timer_init(timer, &fifo, 1);
  timer->waiter = task;
  timer_settime(timer, AHCI_IO_MS / 10 + 1);
  port->regs->is = UINT_MAX;
  port->regs->ci = 1;
  while (port->state == AHCI_BUSY && port->host->online &&
         fifo8_status(&fifo) == 0 && monotonic_time_ns() < deadline) {
    task_fall_blocked_reason(WAITING, WAIT_REASON_DISK);
  }
  timer_free(timer);
  bool complete = port->state == AHCI_DONE;
  port->state = complete ? AHCI_IDLE : AHCI_FAILED;
  irq_restore(saved);
  dma_sync_for_cpu(header, sizeof(*header));
  if (!complete || header->transferred != bytes) {
    ahci_log("ahci: port=%u command=%02x completion=%u bytes=%u/%u is=%08x\n",
             port->index, command, complete, header->transferred, bytes,
             port->interrupt_status);
    ahci_fail_port(port, complete ? "short DMA transfer" : "command timeout or error");
    return false;
  }
  if (!write && bytes != 0) {
    dma_sync_for_cpu(port->buffer, bytes);
  }
  return true;
}

static bool ahci_transfer(char drive, unsigned char *buffer, unsigned count,
                           unsigned lba, bool write) {
  unsigned index = (unsigned)(drive - 'A');
  ahci_port_t *port = index < 26 ? ahci_drives[index] : NULL;
  if (port == NULL || port->state != AHCI_IDLE || !port->host->online ||
      buffer == NULL || count > AHCI_BUFFER_BYTES / 512 ||
      (uint64_t)lba + count > port->sectors) {
    return false;
  }
  if (count == 0) {
    return true;
  }
  if (write) {
    memcpy(port->buffer, buffer, count * 512);
  }
  uint8_t command = write ? (port->lba48 ? AHCI_WRITE_EXT : AHCI_WRITE)
                          : (port->lba48 ? AHCI_READ_EXT : AHCI_READ);
  if (!ahci_execute(port, command, lba, count, count * 512, write)) {
    return false;
  }
  if (!write) {
    memcpy(buffer, port->buffer, count * 512);
  }
  return true;
}

static bool ahci_vdisk_read(char drive, unsigned char *buffer, unsigned count, unsigned lba) {
  return ahci_transfer(drive, buffer, count, lba, false);
}
static bool ahci_vdisk_write(char drive, unsigned char *buffer, unsigned count, unsigned lba) {
  return ahci_transfer(drive, buffer, count, lba, true);
}
static bool ahci_vdisk_sync(char drive) {
  unsigned index = (unsigned)(drive - 'A');
  ahci_port_t *port = index < 26 ? ahci_drives[index] : NULL;
  if (port == NULL || !port->host->online || port->state != AHCI_IDLE) {
    return false;
  }
  return port->flush_command == 0 || ahci_execute(port, port->flush_command, 0, 0, 0, false);
}

static bool ahci_port_initialize(ahci_port_t *port) {
  if (!ahci_stop_port(port)) {
    ahci_fail_port(port, "initial engine stop timeout");
    return false;
  }
  port->control = page_malloc(AHCI_CONTROL_BYTES);
  port->buffer = page_malloc(AHCI_BUFFER_BYTES);
  if (port->control == NULL || port->buffer == NULL ||
      !dma_map(port->control, AHCI_CONTROL_BYTES, port->host->dma_limit, &port->control_address) ||
      !dma_map(port->buffer, AHCI_BUFFER_BYTES, port->host->dma_limit, &port->buffer_address)) {
    ahci_report(port->host, port->index, "DMA allocation/address limit failure");
    return false;
  }
  memset(port->control, 0, AHCI_CONTROL_BYTES);
  memset(port->buffer, 0, AHCI_BUFFER_BYTES);
  port->regs->clb = port->control_address;
  port->regs->clbu = port->control_address >> 32;
  port->regs->fb = port->control_address + AHCI_FIS_OFFSET;
  port->regs->fbu = (port->control_address + AHCI_FIS_OFFSET) >> 32;
  dma_sync_for_device(port->control, AHCI_CONTROL_BYTES);
  port->regs->serr = UINT_MAX;
  port->regs->is = UINT_MAX;
  port->regs->ie = AHCI_IRQ_ERROR | AHCI_IRQ_COMPLETE;
  port->regs->cmd = (port->regs->cmd & ~(0xfu << 28)) |
                   AHCI_CMD_POWER | AHCI_CMD_SPINUP | AHCI_CMD_FIS | (1u << 28);
  port->halted = false;
  uint32_t control = (port->regs->sctl & ~0xf0fu) | (3u << 8);
  port->regs->sctl = control | 1; /* COMRESET for at least 1 ms. */
  sleep(1);
  port->regs->sctl = control;
  if (!ahci_wait(&port->regs->ssts, 0xf, 3, AHCI_SETUP_MS) ||
      !ahci_wait(&port->regs->tfd, AHCI_TFD_BUSY | AHCI_TFD_DRQ, 0, AHCI_SETUP_MS)) {
    ahci_fail_port(port, "SATA link/device readiness timeout");
    return false;
  }
  port->regs->serr = UINT_MAX;
  if (port->regs->sig != AHCI_ATA_SIGNATURE) {
    ahci_report(port->host, port->index, port->regs->sig == AHCI_ATAPI_SIGNATURE
                 ? "ATAPI optical device unsupported" : "unsupported SATA signature");
    return false;
  }
  port->regs->cmd |= AHCI_CMD_START;
  port->state = AHCI_IDLE;
  ahci_log("ahci: port=%u identify start\n", port->index);
  if (!ahci_execute(port, AHCI_IDENTIFY, 0, 0, 512, false)) {
    return false;
  }
  const uint16_t *words = port->buffer;
  bool command_set_valid = (words[83] & 0xc000) == 0x4000;
  port->lba48 = command_set_valid && (words[83] & (1u << 10));
  port->sectors = port->lba48
      ? (uint64_t)words[100] | (uint64_t)words[101] << 16 |
        (uint64_t)words[102] << 32 | (uint64_t)words[103] << 48
      : (uint32_t)words[60] | (uint32_t)words[61] << 16;
  if ((words[49] & 0x300) != 0x300 || port->sectors == 0 ||
      port->sectors > (port->lba48 ? 1ull << 48 : 1ull << 28) ||
      ((words[106] & 0xd000) == 0x5000 &&
       ((uint32_t)words[117] | (uint32_t)words[118] << 16) != 256)) {
    ahci_report(port->host, port->index, "unsupported DMA/LBA capacity or logical sector size");
    return false;
  }
  port->flush_command = command_set_valid && (words[83] & (1u << 13)) ? AHCI_FLUSH_EXT
                       : command_set_valid && (words[83] & (1u << 12)) ? AHCI_FLUSH : 0;
  if (port->flush_command == 0 && (words[85] & (1u << 5))) {
    ahci_report(port->host, port->index, "write cache enabled without flush support");
    return false;
  }
  vdisk disk = {.flag = VDISK_TYPE_BLOCK, .Read = ahci_vdisk_read,
                 .Write = ahci_vdisk_write, .Sync = ahci_vdisk_sync,
                 .size = port->sectors * 512,
                 .max_transfer_sectors = AHCI_BUFFER_BYTES / 512};
  const pci_device_t *pci = port->host->pci;
  snprintf(disk.DriveName, sizeof(disk.DriveName), "AHCI-%04x:%02x:%02x.%u-%u",
           pci->segment, pci->bus, pci->slot, pci->function, port->index);
  for (char drive = 'C'; drive <= 'Z'; drive++) {
    if (register_vdisk_at(drive, disk)) {
      port->drive = drive;
      ahci_drives[drive - 'A'] = port;
      ahci_log("ahci: port=%u drive=%c sectors=%llu lba=%u ready\n", port->index,
               drive, (unsigned long long)port->sectors, port->lba48 ? 48 : 28);
      return true;
    }
  }
  ahci_report(port->host, port->index, "no available disk letter");
  return false;
}

static bool ahci_host_initialize(ahci_host_t *host) {
  pci_bar_t bar;
  if (!pci_read_bar(host->pci, 5, &bar) || bar.type == PCI_BAR_IO || bar.size < 0x100) {
    ahci_report(host, AHCI_PORTS, "invalid ABAR");
    return false;
  }
  pci_command_update(host->pci, PCI_COMMAND_MEMORY, 0);
  host->regs = arch_mmio_map(bar.address, 0x100);
  if (host->regs == NULL || host->regs->cap == UINT_MAX || host->regs->version < 0x10000) {
    ahci_report(host, AHCI_PORTS, "controller registers unavailable");
    return false;
  }
  host->implemented = host->regs->pi;
  unsigned count = host->implemented ? 32u - __builtin_clz(host->implemented) : 0;
  size_t span = 0x100 + count * sizeof(ahci_port_regs_t);
  if (span > bar.size || (host->regs = arch_mmio_map(bar.address, span)) == NULL) {
    ahci_report(host, AHCI_PORTS, "port registers exceed ABAR");
    return false;
  }
  const pci_device_t *pci = host->pci;
  ahci_log("ahci: %04x:%02x:%02x.%u BAR=%llx version=%08x ports=%08x\n",
           pci->segment, pci->bus, pci->slot, pci->function,
           (unsigned long long)bar.address, host->regs->version, host->implemented);
  if (host->regs->version >= 0x10200 && (host->regs->cap2 & 1)) {
    host->regs->bohc |= 2; /* OS-owned semaphore. */
    if (!ahci_wait(&host->regs->bohc, (1u << 0) | (1u << 4), 0, 2000)) {
      ahci_report(host, AHCI_PORTS, "firmware ownership timeout");
      return false;
    }
    host->regs->bohc = (host->regs->bohc & ~(1u << 2)) | (1u << 3);
  }
  host->regs->ghc = (host->regs->ghc | AHCI_GHC_ENABLE) & ~AHCI_GHC_IRQ;
  host->regs->ghc |= AHCI_GHC_RESET;
  if (!ahci_wait(&host->regs->ghc, AHCI_GHC_RESET, 0, AHCI_SETUP_MS)) {
    ahci_report(host, AHCI_PORTS, "controller reset timeout");
    return false;
  }
  host->regs->ghc = (host->regs->ghc | AHCI_GHC_ENABLE) & ~AHCI_GHC_IRQ;
  host->dma_limit = (host->regs->cap & (1u << 31)) ? UINT64_MAX : UINT_MAX;
  for (unsigned i = 0; i < count; i++) {
    if (host->implemented & (1u << i)) {
      host->regs->ports[i].ie = 0;
      host->regs->ports[i].is = UINT_MAX;
    }
  }
  host->regs->is = UINT_MAX;
  if (!pci_irq_initialize(host->pci, ahci_interrupt, &host->interrupt)) {
    ahci_report(host, AHCI_PORTS, "PCI interrupt setup failed");
    return false;
  }
  host->online = true;
  pci_command_update(host->pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0);
  host->regs->ghc |= AHCI_GHC_IRQ;
  static const char *const transports[] = {"none", "intx", "msi", "msix"};
  ahci_log("ahci: transport=%s irq=%u ready\n", transports[host->interrupt.mode], host->interrupt.irq);
  return true;
}

void ahci_init(void) {
  if (ahci_hosts != NULL) {
    return;
  }
  const pci_device_t *pci = NULL;
  unsigned controllers = 0, disks = 0;
  while ((pci = pci_find_class(1, 6, pci)) != NULL) {
    if (pci->programming_interface != 1) {
      continue;
    }
    controllers++;
    ahci_host_t *host = malloc(sizeof(*host));
    if (host == NULL) {
      break;
    }
    memset(host, 0, sizeof(*host));
    host->pci = pci;
    host->next = ahci_hosts;
    ahci_hosts = host;
    if (!ahci_host_initialize(host)) {
      if (host->regs != NULL) {
        host->regs->ghc &= ~AHCI_GHC_IRQ;
      }
      pci_command_update(pci, PCI_COMMAND_INTX_DISABLE, PCI_COMMAND_BUS_MASTER);
      pci_irq_release(&host->interrupt);
      ahci_hosts = host->next;
      free(host);
      continue;
    }
    uint32_t pending = host->implemented;
    while (host->online && pending != 0) {
      unsigned index = __builtin_ctz(pending);
      pending &= pending - 1;
      volatile ahci_port_regs_t *regs = &host->regs->ports[index];
      unsigned detect = regs->ssts & 15;
      if (detect != 1 && detect != 3) {
        continue;
      }
      ahci_port_t *port = malloc(sizeof(*port));
      if (port == NULL) {
        break;
      }
      memset(port, 0, sizeof(*port));
      port->host = host;
      port->regs = regs;
      port->index = index;
      host->ports[index] = port;
      if (ahci_port_initialize(port)) {
        disks++;
        continue;
      }
      if (!port->halted && !ahci_stop_port(port)) {
        ahci_fail_port(port, "port cleanup timeout");
      }
      host->ports[index] = NULL;
      if (port->halted) {
        if (port->buffer != NULL) page_free(port->buffer, AHCI_BUFFER_BYTES);
        if (port->control != NULL) page_free(port->control, AHCI_CONTROL_BYTES);
      }
      free(port);
    }
  }
  ahci_log("ahci: controllers=%u disks=%u initialization complete\n", controllers, disks);
}
