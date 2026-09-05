#include <arch/x86/io.h>
#include <dos.h>
#include <dma.h>
#include <drivers.h>
#include <irq.h>
#include <limits.h>
#include <pci.h>

#define ATA_SR_BSY 0x80u
#define ATA_SR_DF 0x20u
#define ATA_SR_DRQ 0x08u
#define ATA_SR_ERR 0x01u

#define ATA_CMD_READ_DMA 0xc8u
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA 0xcau
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_CACHE_FLUSH 0xe7u
#define ATA_CMD_CACHE_FLUSH_EXT 0xeau
#define ATA_CMD_PACKET 0xa0u
#define ATA_CMD_IDENTIFY_PACKET 0xa1u
#define ATA_CMD_IDENTIFY 0xecu

#define ATAPI_CMD_READ 0xa8u
#define ATAPI_FEATURE_DMA 0x01u

#define ATA_IDENT_MODEL 54u
#define ATA_IDENT_CAPABILITIES 98u
#define ATA_IDENT_MAX_LBA 120u
#define ATA_IDENT_COMMANDSETS 164u
#define ATA_IDENT_MAX_LBA_EXT 200u

#define IDE_ATA 0u
#define IDE_ATAPI 1u
#define ATA_PRIMARY 0u
#define ATA_SECONDARY 1u
#define ATA_READ 0u
#define ATA_WRITE 1u

#define ATA_REG_FEATURES 1u
#define ATA_REG_SECCOUNT0 2u
#define ATA_REG_LBA0 3u
#define ATA_REG_LBA1 4u
#define ATA_REG_LBA2 5u
#define ATA_REG_HDDEVSEL 6u
#define ATA_REG_COMMAND 7u
#define ATA_REG_STATUS 7u
#define ATA_REG_SECCOUNT1 8u
#define ATA_REG_LBA3 9u
#define ATA_REG_LBA4 10u
#define ATA_REG_LBA5 11u
#define ATA_REG_CONTROL 12u
#define ATA_REG_ALTSTATUS 12u

#define IDE_ATA_SECTOR_BYTES 512u
#define IDE_ATAPI_SECTOR_BYTES 2048u
#define IDE_DMA_BUFFER_BYTES 0x10000u
#define IDE_DMA_MAX_ATA_SECTORS                                            \
  (IDE_DMA_BUFFER_BYTES / IDE_ATA_SECTOR_BYTES)
#define IDE_DMA_MAX_ATAPI_SECTORS                                          \
  (IDE_DMA_BUFFER_BYTES / IDE_ATAPI_SECTOR_BYTES)
#define IDE_BM_COMMAND 0u
#define IDE_BM_STATUS 2u
#define IDE_BM_PRDT 4u
#define IDE_BM_START 0x01u
#define IDE_BM_READ 0x08u
#define IDE_BM_ERROR 0x02u
#define IDE_BM_INTERRUPT 0x04u
#define IDE_PRD_END 0x8000u
#define IDE_POLL_LIMIT 10000000u

typedef struct {
  uint32_t address;
  uint16_t byte_count;
  uint16_t flags;
} __attribute__((packed)) ide_prd_t;

typedef struct {
  uint16_t command_base;
  uint16_t control_port;
  uint16_t bus_master_base;
  uint8_t interrupt_disable;
  ide_prd_t *prdt;
  uint8_t *dma_buffer;
  volatile bool dma_active;
  volatile bool dma_done;
  volatile bool dma_failed;
  mtask *dma_waiter;
} ide_channel_t;

typedef struct {
  uint8_t present;
  uint8_t channel;
  uint8_t drive;
  uint8_t type;
  uint16_t capabilities;
  uint32_t command_sets;
  uint32_t sectors;
  char model[41];
} ide_device_t;

static ide_channel_t ide_channels[2];
static ide_device_t ide_devices[4];
static lock_t ide_controller_lock;
static int ide_active_channel = -1;

static inline void ide_read_data32(uint16_t port, void *buffer,
                                   uint32_t dwords) {
  x86_port_read32s(port, buffer, dwords);
}

static inline void ide_write_data16(uint16_t port, const void *buffer,
                                    uint32_t words) {
  x86_port_write16s(port, buffer, words);
}

static uint8_t ide_register_read(uint8_t channel, uint8_t reg) {
  ide_channel_t *controller = &ide_channels[channel];
  bool high = reg >= ATA_REG_SECCOUNT1 && reg <= ATA_REG_LBA5;
  if (high) {
    x86_port_write8(controller->control_port,
                    0x80u | controller->interrupt_disable);
  }
  uint8_t value;
  if (reg <= ATA_REG_STATUS) {
    value = x86_port_read8(controller->command_base + reg);
  } else if (high) {
    value = x86_port_read8(controller->command_base + reg - 6u);
  } else {
    value = x86_port_read8(controller->control_port);
  }
  if (high) {
    x86_port_write8(controller->control_port,
                    controller->interrupt_disable);
  }
  return value;
}

static void ide_register_write(uint8_t channel, uint8_t reg, uint8_t value) {
  ide_channel_t *controller = &ide_channels[channel];
  bool high = reg >= ATA_REG_SECCOUNT1 && reg <= ATA_REG_LBA5;
  if (high) {
    x86_port_write8(controller->control_port,
                    0x80u | controller->interrupt_disable);
  }
  if (reg <= ATA_REG_COMMAND) {
    x86_port_write8(controller->command_base + reg, value);
  } else if (high) {
    x86_port_write8(controller->command_base + reg - 6u, value);
  } else if (reg == ATA_REG_CONTROL) {
    controller->interrupt_disable = value & 0x02u;
    x86_port_write8(controller->control_port, value);
  }
  if (high) {
    x86_port_write8(controller->control_port,
                    controller->interrupt_disable);
  }
}

static void ide_delay_400ns(uint8_t channel) {
  for (unsigned int i = 0; i < 4; i++) {
    (void)ide_register_read(channel, ATA_REG_ALTSTATUS);
  }
}

static bool ide_wait(uint8_t channel, bool require_drq, bool require_idle) {
  for (unsigned int spins = 0; spins < IDE_POLL_LIMIT; spins++) {
    uint8_t status = ide_register_read(channel, ATA_REG_STATUS);
    if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
      return false;
    }
    if ((status & ATA_SR_BSY) == 0 &&
        (!require_drq || (status & ATA_SR_DRQ) != 0) &&
        (!require_idle || (status & ATA_SR_DRQ) == 0)) {
      return true;
    }
    if ((spins & 0x3ffu) == 0) {
      scheduler_preempt_if_needed();
    }
  }
  return false;
}

static bool ide_pci_bus_master_initialize(void) {
  const pci_device_t *controller = pci_find_class(0x01, 0x01, NULL);
  if (controller == NULL) {
    logk("ide: PCI controller unavailable\n");
    return false;
  }
  pci_bar_t bus_master;
  if ((controller->programming_interface & 0x80u) == 0 ||
      (controller->programming_interface & 0x05u) != 0 ||
      !pci_read_bar(controller, 4, &bus_master) ||
      bus_master.type != PCI_BAR_IO || bus_master.address > 0xfff0u) {
    logk("ide: PCI bus-master DMA unavailable\n");
    return false;
  }
  uint32_t base = bus_master.address;
  pci_command_update(controller, PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER, 0);
  ide_channels[ATA_PRIMARY].bus_master_base = (uint16_t)base;
  ide_channels[ATA_SECONDARY].bus_master_base = (uint16_t)(base + 8u);
  logk("ide: bus-master DMA io=%04x\n", base);
  return true;
}

static int ide_identify_device(uint8_t channel, uint8_t drive,
                               ide_device_t *device) {
  ide_register_write(channel, ATA_REG_HDDEVSEL, 0xa0u | (drive << 4));
  ide_delay_400ns(channel);
  ide_register_write(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
  uint8_t status = ide_register_read(channel, ATA_REG_STATUS);
  if (status == 0) {
    return 0;
  }

  bool atapi = false;
  for (unsigned int spins = 0; spins < IDE_POLL_LIMIT; spins++) {
    status = ide_register_read(channel, ATA_REG_STATUS);
    if ((status & ATA_SR_ERR) != 0) {
      uint8_t signature_low = ide_register_read(channel, ATA_REG_LBA1);
      uint8_t signature_high = ide_register_read(channel, ATA_REG_LBA2);
      if (!((signature_low == 0x14u && signature_high == 0xebu) ||
            (signature_low == 0x69u && signature_high == 0x96u))) {
        return 0;
      }
      atapi = true;
      ide_register_write(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
      break;
    }
    if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ) != 0) {
      break;
    }
    if ((spins & 0x3ffu) == 0) {
      scheduler_preempt_if_needed();
    }
  }
  if (!ide_wait(channel, true, false)) {
    return 0;
  }

  uint8_t identify[512];
  ide_read_data32(ide_channels[channel].command_base, identify, 128);
  memset(device, 0, sizeof(*device));
  device->present = 1;
  device->channel = channel;
  device->drive = drive;
  device->type = atapi ? IDE_ATAPI : IDE_ATA;
  device->capabilities =
      *(uint16_t *)(void *)(identify + ATA_IDENT_CAPABILITIES);
  device->command_sets =
      *(uint32_t *)(void *)(identify + ATA_IDENT_COMMANDSETS);
  uint32_t lba28 = *(uint32_t *)(void *)(identify + ATA_IDENT_MAX_LBA);
  uint32_t lba48 = *(uint32_t *)(void *)(identify + ATA_IDENT_MAX_LBA_EXT);
  device->sectors =
      (device->command_sets & (1u << 26)) != 0 && lba48 != 0 ? lba48 : lba28;
  for (unsigned int i = 0; i < 40; i += 2) {
    device->model[i] = identify[ATA_IDENT_MODEL + i + 1];
    device->model[i + 1] = identify[ATA_IDENT_MODEL + i];
  }
  device->model[40] = '\0';
  return 1;
}

static bool ide_dma_build_prdt(ide_channel_t *channel, uint32_t bytes) {
  if (bytes == 0 || bytes > IDE_DMA_BUFFER_BYTES || channel->prdt == NULL ||
      channel->dma_buffer == NULL) {
    return false;
  }
  memset(channel->prdt, 0, 4096);
  dma_addr_t mapped;
  if (!dma_map(channel->dma_buffer, bytes, UINT_MAX, &mapped)) {
    return false;
  }
  uint32_t address = (uint32_t)mapped;
  uint32_t remaining = bytes;
  unsigned int count = 0;
  while (remaining != 0) {
    if (count >= 2) {
      return false;
    }
    uint32_t boundary = 0x10000u - (address & 0xffffu);
    uint32_t chunk = remaining < boundary ? remaining : boundary;
    channel->prdt[count].address = address;
    channel->prdt[count].byte_count =
        chunk == 0x10000u ? 0 : (uint16_t)chunk;
    address += chunk;
    remaining -= chunk;
    count++;
  }
  channel->prdt[count - 1].flags = IDE_PRD_END;
  dma_sync_for_device(channel->prdt, count * sizeof(channel->prdt[0]));
  return true;
}

static void ide_dma_block(ide_channel_t *channel) {
  mtask *task = current_task();
  while (!channel->dma_done) {
    irq_state_t state = irq_save();
    if (channel->dma_done) {
      task->ready = 0;
      irq_restore(state);
      break;
    }
    irq_restore(state);
    task_fall_blocked_reason(WAITING, WAIT_REASON_DISK);
  }
  task->ready = 0;
}

static uint8_t ide_dma_transfer(uint8_t direction, uint8_t drive,
                                uint32_t lba, uint8_t sectors, void *buffer) {
  ide_device_t *device = &ide_devices[drive];
  ide_channel_t *channel = &ide_channels[device->channel];
  uint32_t sector_bytes = device->type == IDE_ATAPI
                              ? IDE_ATAPI_SECTOR_BYTES
                              : IDE_ATA_SECTOR_BYTES;
  uint32_t bytes = (uint32_t)sectors * sector_bytes;
  if (sectors == 0 || bytes > IDE_DMA_BUFFER_BYTES || buffer == NULL ||
      (device->type == IDE_ATAPI && direction != ATA_READ) ||
      channel->bus_master_base == 0 || current_task() == NULL ||
      !ide_dma_build_prdt(channel, bytes) ||
      !ide_wait(device->channel, false, true)) {
    return 2;
  }

  uint8_t lba_mode = 0;
  uint8_t head = 0;
  uint8_t lba_io[6] = {0};
  uint8_t packet[12] = {0};
  if (device->type == IDE_ATAPI) {
    packet[0] = ATAPI_CMD_READ;
    packet[2] = lba >> 24;
    packet[3] = lba >> 16;
    packet[4] = lba >> 8;
    packet[5] = lba;
    packet[9] = sectors;

    ide_register_write(device->channel, ATA_REG_CONTROL, 2);
    ide_register_write(device->channel, ATA_REG_HDDEVSEL, device->drive << 4);
    ide_delay_400ns(device->channel);
    ide_register_write(device->channel, ATA_REG_FEATURES, ATAPI_FEATURE_DMA);
    ide_register_write(device->channel, ATA_REG_LBA1, bytes);
    ide_register_write(device->channel, ATA_REG_LBA2, bytes >> 8);
    ide_register_write(device->channel, ATA_REG_COMMAND, ATA_CMD_PACKET);
    if (!ide_wait(device->channel, true, false)) {
      ide_register_write(device->channel, ATA_REG_CONTROL, 0);
      return 2;
    }
  } else if (lba >= 0x10000000u) {
    if ((device->command_sets & (1u << 26)) == 0) {
      return 2;
    }
    lba_mode = 2;
    lba_io[0] = lba;
    lba_io[1] = lba >> 8;
    lba_io[2] = lba >> 16;
    lba_io[3] = lba >> 24;
  } else if ((device->capabilities & 0x200u) != 0) {
    lba_mode = 1;
    lba_io[0] = lba;
    lba_io[1] = lba >> 8;
    lba_io[2] = lba >> 16;
    head = lba >> 24;
  } else {
    uint32_t sector = lba % 63u + 1u;
    uint32_t cylinder = (lba + 1u - sector) / (16u * 63u);
    lba_io[0] = sector;
    lba_io[1] = cylinder;
    lba_io[2] = cylinder >> 8;
    head = (lba + 1u - sector) % (16u * 63u) / 63u;
  }

  if (direction == ATA_WRITE) {
    memcpy(channel->dma_buffer, buffer, bytes);
    dma_sync_for_device(channel->dma_buffer, bytes);
  }
  uint16_t bus_master = channel->bus_master_base;
  uint8_t bus_master_command = direction == ATA_READ ? IDE_BM_READ : 0;
  dma_addr_t prdt_address;
  if (!dma_map(channel->prdt, 4096, UINT_MAX, &prdt_address)) {
    return 2;
  }
  irq_state_t state = irq_save();
  ide_active_channel = device->channel;
  channel->dma_active = true;
  channel->dma_done = false;
  channel->dma_failed = false;
  channel->dma_waiter = current_task();
  channel->dma_waiter->ready = 0;

  x86_port_write8(bus_master + IDE_BM_COMMAND, 0);
  uint8_t bus_master_status = x86_port_read8(bus_master + IDE_BM_STATUS);
  x86_port_write8(bus_master + IDE_BM_STATUS,
                   bus_master_status | IDE_BM_ERROR | IDE_BM_INTERRUPT);
  x86_port_write32(bus_master + IDE_BM_PRDT, (uint32_t)prdt_address);
  x86_port_write8(bus_master + IDE_BM_COMMAND, bus_master_command);

  __atomic_thread_fence(__ATOMIC_RELEASE);
  ide_register_write(device->channel, ATA_REG_CONTROL, 0);
  if (device->type == IDE_ATAPI) {
    ide_write_data16(channel->command_base, packet, 6);
  } else {
    ide_register_write(device->channel, ATA_REG_HDDEVSEL,
                       (lba_mode == 0 ? 0xa0u : 0xe0u) |
                           (device->drive << 4) | head);
    ide_delay_400ns(device->channel);
    if (lba_mode == 2) {
      ide_register_write(device->channel, ATA_REG_SECCOUNT1, 0);
      ide_register_write(device->channel, ATA_REG_LBA3, lba_io[3]);
      ide_register_write(device->channel, ATA_REG_LBA4, lba_io[4]);
      ide_register_write(device->channel, ATA_REG_LBA5, lba_io[5]);
    }
    ide_register_write(device->channel, ATA_REG_SECCOUNT0, sectors);
    ide_register_write(device->channel, ATA_REG_LBA0, lba_io[0]);
    ide_register_write(device->channel, ATA_REG_LBA1, lba_io[1]);
    ide_register_write(device->channel, ATA_REG_LBA2, lba_io[2]);
    uint8_t command = direction == ATA_READ
                          ? (lba_mode == 2 ? ATA_CMD_READ_DMA_EXT
                                           : ATA_CMD_READ_DMA)
                          : (lba_mode == 2 ? ATA_CMD_WRITE_DMA_EXT
                                           : ATA_CMD_WRITE_DMA);
    ide_register_write(device->channel, ATA_REG_COMMAND, command);
  }
  x86_port_write8(bus_master + IDE_BM_COMMAND,
                   bus_master_command | IDE_BM_START);
  irq_restore(state);

  ide_dma_block(channel);
  state = irq_save();
  bool failed = channel->dma_failed;
  channel->dma_waiter = NULL;
  irq_restore(state);
  if (failed) {
    return 2;
  }
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  if (direction == ATA_READ) {
    dma_sync_for_cpu(channel->dma_buffer, bytes);
    memcpy(buffer, channel->dma_buffer, bytes);
  } else if (device->type == IDE_ATA) {
    ide_register_write(device->channel, ATA_REG_CONTROL, 2);
    ide_register_write(device->channel, ATA_REG_COMMAND,
                       lba_mode == 2 ? ATA_CMD_CACHE_FLUSH_EXT
                                     : ATA_CMD_CACHE_FLUSH);
    bool flushed = ide_wait(device->channel, false, true);
    ide_register_write(device->channel, ATA_REG_CONTROL, 0);
    if (!flushed) {
      return 2;
    }
  }
  return 0;
}

static void ide_report_error(uint8_t drive, uint8_t error) {
  if (error == 0) {
    return;
  }
  ide_device_t *device = drive < 4 ? &ide_devices[drive] : NULL;
  if (device != NULL && device->present && device->type == IDE_ATAPI) {
    return;
  }
  logk("ide: I/O error=%d drive=%d model=%s\n", error, drive,
       device != NULL && device->present ? device->model : "unknown");
}

bool ide_read_sectors(unsigned char drive, unsigned char sectors,
                      unsigned int lba, unsigned short selector, void *buffer) {
  (void)selector;
  uint8_t error = 0;
  if (drive >= 4 || !ide_devices[drive].present || sectors == 0 ||
      (ide_devices[drive].type == IDE_ATA &&
       (lba >= ide_devices[drive].sectors ||
        sectors > ide_devices[drive].sectors - lba))) {
    error = 1;
  } else {
    lock(&ide_controller_lock);
    error = ide_dma_transfer(ATA_READ, drive, lba, sectors, buffer);
    unlock(&ide_controller_lock);
  }
  ide_report_error(drive, error);
  return error == 0;
}

bool ide_write_sectors(unsigned char drive, unsigned char sectors,
                       unsigned int lba, unsigned short selector,
                       void *buffer) {
  (void)selector;
  uint8_t error = 0;
  if (drive >= 4 || !ide_devices[drive].present || sectors == 0 ||
      ide_devices[drive].type != IDE_ATA ||
      lba >= ide_devices[drive].sectors ||
      sectors > ide_devices[drive].sectors - lba) {
    error = 1;
  } else {
    lock(&ide_controller_lock);
    error = ide_dma_transfer(ATA_WRITE, drive, lba, sectors, buffer);
    unlock(&ide_controller_lock);
  }
  ide_report_error(drive, error);
  return error == 0;
}

static bool ide_vdisk_read(char drive, unsigned char *buffer,
                           unsigned int sectors, unsigned int lba) {
  return ide_read_sectors((uint8_t)(drive - 'C'), (uint8_t)sectors, lba, 0,
                          buffer);
}

static bool ide_vdisk_write(char drive, unsigned char *buffer,
                            unsigned int sectors, unsigned int lba) {
  return ide_write_sectors((uint8_t)(drive - 'C'), (uint8_t)sectors, lba, 0,
                           buffer);
}

bool ide_irq(unsigned irq) {
  int active = ide_active_channel;
  if (active >= 0 && active < 2) {
    ide_channel_t *channel = &ide_channels[active];
    if (channel->dma_active) {
      uint16_t bus_master = channel->bus_master_base;
      uint8_t command = x86_port_read8(bus_master + IDE_BM_COMMAND);
      x86_port_write8(bus_master + IDE_BM_COMMAND,
                      command & ~IDE_BM_START);
      uint8_t bus_master_status = x86_port_read8(bus_master + IDE_BM_STATUS);
      x86_port_write8(bus_master + IDE_BM_STATUS,
                       bus_master_status | IDE_BM_ERROR | IDE_BM_INTERRUPT);
      uint8_t ata_status = ide_register_read(active, ATA_REG_STATUS);
      channel->dma_failed =
          (bus_master_status & IDE_BM_ERROR) != 0 ||
          (ata_status & (ATA_SR_ERR | ATA_SR_DF)) != 0;
      channel->dma_active = false;
      channel->dma_done = true;
      ide_active_channel = -1;
      __atomic_thread_fence(__ATOMIC_RELEASE);
      if (channel->dma_waiter != NULL) {
        task_run(channel->dma_waiter);
      }
    } else {
      (void)ide_register_read(active, ATA_REG_STATUS);
    }
  }
  return false;
}

void ide_initialize(void) {
  if (!irq_register_handler(14, ide_irq, IRQ_EXCLUSIVE) ||
      !irq_register_handler(15, ide_irq, IRQ_EXCLUSIVE)) {
    logk("ide: unable to register interrupts\n");
    return;
  }
  memset(ide_channels, 0, sizeof(ide_channels));
  memset(ide_devices, 0, sizeof(ide_devices));
  ide_channels[ATA_PRIMARY].command_base = 0x1f0;
  ide_channels[ATA_PRIMARY].control_port = 0x3f6;
  ide_channels[ATA_SECONDARY].command_base = 0x170;
  ide_channels[ATA_SECONDARY].control_port = 0x376;
  lock_init(&ide_controller_lock);

  bool dma_available = ide_pci_bus_master_initialize();
  if (dma_available) {
    for (unsigned int channel = 0; channel < 2; channel++) {
      ide_channels[channel].prdt = page_malloc(4096);
      ide_channels[channel].dma_buffer = page_malloc(IDE_DMA_BUFFER_BYTES);
      if (ide_channels[channel].prdt == NULL ||
          ide_channels[channel].dma_buffer == NULL) {
        ide_channels[channel].bus_master_base = 0;
      }
    }
  }

  ide_register_write(ATA_PRIMARY, ATA_REG_CONTROL, 2);
  ide_register_write(ATA_SECONDARY, ATA_REG_CONTROL, 2);
  unsigned int count = 0;
  for (unsigned int channel = 0; channel < 2 && count < 4; channel++) {
    for (unsigned int drive = 0; drive < 2 && count < 4; drive++) {
      if (ide_identify_device(channel, drive, &ide_devices[count])) {
        count++;
      }
    }
  }
  ide_register_write(ATA_PRIMARY, ATA_REG_CONTROL, 0);
  ide_register_write(ATA_SECONDARY, ATA_REG_CONTROL, 0);
  irq_mask_clear(14);
  irq_mask_clear(15);

  for (unsigned int i = 0; i < count; i++) {
    ide_device_t *device = &ide_devices[i];
    if (ide_channels[device->channel].bus_master_base == 0 ||
        (device->capabilities & 0x100u) == 0) {
      logk("ide: %s lacks usable DMA support\n", device->model);
      continue;
    }
    vdisk disk = {0};
    disk.Read = ide_vdisk_read;
    disk.Write = ide_vdisk_write;
    disk.flag = device->type == IDE_ATAPI ? VDISK_TYPE_OPTICAL
                                          : VDISK_TYPE_BLOCK;
    disk.size = (uint64_t)device->sectors * IDE_ATA_SECTOR_BYTES;
    disk.max_transfer_sectors =
        device->type == IDE_ATAPI ? IDE_DMA_MAX_ATAPI_SECTORS
                                  : IDE_DMA_MAX_ATA_SECTORS;
    strcpy(disk.DriveName, "PCI IDE");
    register_vdisk_at('C' + i, disk);
    logk("ide: %s DMA %s sectors=%d drive=%c\n",
         device->type == IDE_ATAPI ? "ATAPI" : "ATA", device->model,
         device->sectors, 'C' + i);
  }
}
