#include <dos.h>
#include <limits.h>
#include <stdint.h>
#include <usb.h>

enum {
  USB_BOT_CBW_SIGNATURE = 0x43425355,
  USB_BOT_CSW_SIGNATURE = 0x53425355,
  USB_STORAGE_TRANSFER_SECTORS = 128,
  STORAGE_INVALID_OPCODE = -4,
  STORAGE_INVALID_FIELD = -5,
};

typedef enum {
  STORAGE_CACHE_UNKNOWN,
  STORAGE_CACHE_UNREPORTED,
  STORAGE_CACHE_WRITE_THROUGH,
  STORAGE_CACHE_WRITE_BACK,
} storage_cache_t;

typedef struct __attribute__((packed)) {
  uint32_t signature, tag, length;
  uint8_t flags, lun, command_length, command[16];
} usb_cbw_t;

typedef struct __attribute__((packed)) {
  uint32_t signature, tag, residue;
  uint8_t status;
} usb_csw_t;

_Static_assert(sizeof(usb_cbw_t) == 31, "BOT CBW layout");
_Static_assert(sizeof(usb_csw_t) == 13, "BOT CSW layout");

typedef struct usb_storage usb_storage_t;
typedef struct usb_storage_unit {
  struct usb_storage_unit *next;
  usb_storage_t *storage;
  uint64_t sectors;
  uint32_t block_size;
  storage_cache_t cache;
  uint8_t lun;
  bool read_only;
} usb_storage_unit_t;

typedef struct usb_volume {
  struct usb_volume *next;
  usb_storage_unit_t *unit;
  uint64_t first, sectors;
  char drive;
} usb_volume_t;

struct usb_storage {
  usb_device_t *device;
  usb_storage_unit_t *units;
  usb_volume_t *volumes;
  unsigned references;
  uint32_t tag;
  uint8_t interface, in, out;
  bool online;
};

typedef struct {
  usb_work_t work;
  usb_volume_t *volume;
  uint64_t block;
  unsigned blocks, offset, length;
  enum { STORAGE_READ, STORAGE_WRITE, STORAGE_SYNC } operation;
  bool success;
  char error[DISK_INFO_ERROR_SIZE];
  uint8_t bytes[];
} usb_storage_work_t;

static usb_volume_t *usb_drives[26];

static uint64_t scsi_number(const uint8_t *bytes, unsigned length) {
  uint64_t value = 0;
  for (unsigned i = 0; i < length; i++) {
    value = value << 8 | bytes[i];
  }
  return value;
}

static void scsi_number_store(uint8_t *bytes, uint64_t value, unsigned length) {
  while (length != 0) {
    bytes[--length] = value;
    value >>= 8;
  }
}

static uint32_t storage_le32(const uint8_t *bytes) {
  return bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
         (uint32_t)bytes[3] << 24;
}

static bool storage_reset(usb_storage_t *storage) {
  usb_device_t *device = storage->device;
  if (!usb_connected(device)) {
    return false;
  }
  usb_setup_t setup = {
      .type = 0x21, .request = 0xff, .index = storage->interface};
  return device->ops->control(device, &setup, NULL) == 0 &&
         device->ops->clear_halt(device, storage->in) &&
         device->ops->clear_halt(device, storage->out);
}

/* The USB service task is the sole BOT owner, including all LUNs. A transport
 * failure is recovered for the next command; writes are never blindly replayed.
 */
static int storage_command(usb_storage_unit_t *unit, const uint8_t *command,
                           unsigned command_length, void *buffer,
                           unsigned length, bool input, unsigned *actual,
                           char *error) {
  usb_storage_t *storage = unit->storage;
  usb_device_t *device = storage->device;
  if (!storage->online || !usb_connected(device)) {
    if (error)
      snprintf(error, DISK_INFO_ERROR_SIZE, "USB cmd=%02x disconnected",
               command[0]);
    return USB_ERROR_DISCONNECTED;
  }
  usb_cbw_t cbw = {.signature = USB_BOT_CBW_SIGNATURE,
                   .tag = ++storage->tag,
                   .length = length,
                   .flags = input ? 0x80 : 0,
                   .lun = unit->lun,
                   .command_length = command_length};
  memcpy(cbw.command, command, command_length);
  int received = device->ops->transfer(device, storage->out, &cbw, sizeof(cbw));
  if (received != sizeof(cbw)) {
    if (error)
      snprintf(error, DISK_INFO_ERROR_SIZE, "USB cmd=%02x CBW result=%d",
               command[0], received);
    storage_reset(storage);
    return USB_ERROR_IO;
  }
  unsigned completed = 0;
  while (completed < length) {
    unsigned chunk = length - completed;
    if (chunk > 65536)
      chunk = 65536;
    uint8_t endpoint = input ? storage->in : storage->out;
    received = device->ops->transfer(device, endpoint,
                                     (uint8_t *)buffer + completed, chunk);
    if (received == USB_ERROR_STALL) {
      if (!device->ops->clear_halt(device, endpoint)) {
        if (error)
          snprintf(error, DISK_INFO_ERROR_SIZE,
                   "USB cmd=%02x data STALL recovery failed", command[0]);
        storage_reset(storage);
        return USB_ERROR_IO;
      }
      break;
    }
    if (received < 0 || (unsigned)received > chunk) {
      if (error)
        snprintf(error, DISK_INFO_ERROR_SIZE,
                 "USB cmd=%02x data result=%d bytes=%u/%u", command[0],
                 received, completed, length);
      storage_reset(storage);
      return USB_ERROR_IO;
    }
    completed += received;
    if ((unsigned)received < chunk)
      break;
  }
  usb_csw_t csw = {0};
  received = device->ops->transfer(device, storage->in, &csw, sizeof(csw));
  if (received == USB_ERROR_STALL &&
      device->ops->clear_halt(device, storage->in)) {
    received = device->ops->transfer(device, storage->in, &csw, sizeof(csw));
  }
  if (received != sizeof(csw) || csw.signature != USB_BOT_CSW_SIGNATURE ||
      csw.tag != cbw.tag || csw.residue > length || csw.status > 1 ||
      (csw.status == 0 && completed + csw.residue != length)) {
    if (error)
      snprintf(error, DISK_INFO_ERROR_SIZE,
               "USB cmd=%02x CSW result=%d sig=%08x tag=%u/%u status=%u "
               "residue=%u data=%u/%u",
               command[0], received, csw.signature, csw.tag, cbw.tag,
               csw.status, csw.residue, completed, length);
    storage_reset(storage);
    return USB_ERROR_IO;
  }
  *actual = completed;
  return csw.status;
}

static int storage_scsi(usb_storage_unit_t *unit, const uint8_t *command,
                        unsigned command_length, void *buffer, unsigned length,
                        bool input, char *error) {
  uint64_t deadline = monotonic_time_ns() + 3000000000ull;
  for (;;) {
    unsigned actual = 0;
    int status = storage_command(unit, command, command_length, buffer, length,
                                 input, &actual, error);
    if (status == 0)
      return actual;
    if (status < 0)
      return status;
    uint8_t sense[18] = {0};
    uint8_t request[6] = {3, 0, 0, 0, sizeof(sense), 0};
    status = storage_command(unit, request, sizeof(request), sense,
                             sizeof(sense), true, &actual, error);
    if (status != 0 || actual < 4) {
      if (error && !error[0])
        snprintf(error, DISK_INFO_ERROR_SIZE,
                 "SCSI cmd=%02x REQUEST SENSE failed: status=%d bytes=%u",
                 command[0], status, actual);
      return USB_ERROR_IO;
    }
    unsigned format = sense[0] & 0x7f;
    bool descriptor = format == 0x72 || format == 0x73;
    if ((!descriptor && format != 0x70 && format != 0x71) ||
        (!descriptor && actual < 14)) {
      if (error)
        snprintf(error, DISK_INFO_ERROR_SIZE,
                 "SCSI cmd=%02x invalid sense: format=%02x bytes=%u",
                 command[0], format, actual);
      return USB_ERROR_IO;
    }
    unsigned key = descriptor ? sense[1] & 15 : sense[2] & 15;
    unsigned asc = descriptor ? sense[2] : sense[12];
    unsigned ascq = descriptor ? sense[3] : sense[13];
    if (monotonic_time_ns() >= deadline ||
        (key != 6 && !(key == 2 && asc == 4))) {
      if (error)
        snprintf(error, DISK_INFO_ERROR_SIZE,
                 "SCSI cmd=%02x sense=%x asc=%02x/%02x", command[0], key, asc,
                 ascq);
      usb_log("usb-storage: SCSI %02x sense=%x asc=%02x/%02x\n", command[0],
              key, asc, ascq);
      if (key == 5 && ascq == 0) {
        if (asc == 0x20)
          return STORAGE_INVALID_OPCODE;
        if (asc == 0x24)
          return STORAGE_INVALID_FIELD;
      }
      return USB_ERROR_IO;
    }
    sleep(100);
  }
}

/* MODE SENSE reports the current caching policy, not whether a flush command
 * happens to be implemented. Keep malformed/transport failures distinct from
 * a device that simply has no caching mode page. */
static void storage_cache_probe(usb_storage_unit_t *unit) {
  uint8_t response[255];
  uint8_t command[10] = {0x1a, 0, 0x3f, 0, sizeof(response), 0};
  int length =
      storage_scsi(unit, command, 6, response, sizeof(response), true, NULL);
  unsigned header = 4;
  if (length == STORAGE_INVALID_OPCODE || length == STORAGE_INVALID_FIELD) {
    memset(command, 0, sizeof(command));
    command[0] = 0x5a;
    command[2] = 0x3f;
    command[8] = sizeof(response);
    length =
        storage_scsi(unit, command, 10, response, sizeof(response), true, NULL);
    header = 8;
  }
  if (length == STORAGE_INVALID_OPCODE || length == STORAGE_INVALID_FIELD) {
    unit->cache = STORAGE_CACHE_UNREPORTED;
    return;
  }
  if (length < (int)header)
    return;
  unit->read_only = (response[header == 4 ? 2 : 3] & 0x80) != 0;
  unsigned total = header == 4 ? response[0] + 1u : scsi_number(response, 2) + 2;
  unsigned offset =
      header + (header == 4 ? response[3] : scsi_number(response + 6, 2));
  if (total > (unsigned)length || offset > total)
    return;
  while (offset < total) {
    const uint8_t *page = response + offset;
    if (total - offset < 2)
      return;
    bool subpage = (page[0] & 0x40) != 0;
    unsigned page_header = subpage ? 4 : 2;
    if (total - offset < page_header)
      return;
    unsigned size =
        page_header + (subpage ? scsi_number(page + 2, 2) : page[1]);
    if (size > total - offset)
      return;
    if (!subpage && (page[0] & 0x3f) == 8) {
      if (size >= 20)
        unit->cache = (page[2] & 4) ? STORAGE_CACHE_WRITE_BACK
                                    : STORAGE_CACHE_WRITE_THROUGH;
      return;
    }
    offset += size;
  }
  unit->cache = STORAGE_CACHE_UNREPORTED;
}

static bool storage_blocks(usb_storage_unit_t *unit, uint64_t block,
                           unsigned blocks, void *bytes, bool read,
                           char *error) {
  uint8_t command[16] = {0};
  unsigned command_length;
  if (block <= UINT_MAX && blocks <= 65535 && blocks - 1 <= UINT_MAX - block) {
    command[0] = read ? 0x28 : 0x2a;
    scsi_number_store(command + 2, block, 4);
    scsi_number_store(command + 7, blocks, 2);
    command_length = 10;
  } else {
    command[0] = read ? 0x88 : 0x8a;
    scsi_number_store(command + 2, block, 8);
    scsi_number_store(command + 10, blocks, 4);
    command_length = 16;
  }
  unsigned length = blocks * unit->block_size;
  int result =
      storage_scsi(unit, command, command_length, bytes, length, read, error);
  if (result >= 0 && (unsigned)result != length && error)
    snprintf(error, DISK_INFO_ERROR_SIZE,
             "SCSI cmd=%02x short transfer bytes=%d/%u", command[0], result,
             length);
  return result == (int)length;
}

static void storage_release(usb_storage_t *storage) {
  if (--storage->references != 0)
    return;
  while (storage->volumes != NULL) {
    usb_volume_t *volume = storage->volumes;
    storage->volumes = volume->next;
    free(volume);
  }
  while (storage->units != NULL) {
    usb_storage_unit_t *unit = storage->units;
    storage->units = unit->next;
    free(unit);
  }
  free(storage);
}

static void storage_work_destroy(usb_work_t *base) {
  usb_storage_work_t *work = (usb_storage_work_t *)base;
  storage_release(work->volume->unit->storage);
  free(work);
}

static void storage_work_execute(usb_work_t *base) {
  usb_storage_work_t *work = (usb_storage_work_t *)base;
  usb_storage_unit_t *unit = work->volume->unit;
  work->success = false;
  if (!unit->storage->online || !usb_connected(unit->storage->device))
    return;
  if (work->operation == STORAGE_SYNC) {
    if (unit->cache == STORAGE_CACHE_WRITE_THROUGH) {
      work->success = true;
      return;
    }
    uint8_t command[10] = {0x35};
    int result = storage_scsi(unit, command, sizeof(command), NULL, 0, false,
                              work->error);
    /* Assume write-through only when both the caching page and SYNCHRONIZE
     * CACHE are unavailable. Never apply this fallback to WCE, failed cache
     * discovery, invalid CDB fields, or real transport/media errors. */
    if (result == STORAGE_INVALID_OPCODE &&
        unit->cache == STORAGE_CACHE_UNREPORTED) {
      unit->cache = STORAGE_CACHE_WRITE_THROUGH;
      work->error[0] = 0;
      usb_log("usb-storage: lun=%u no caching page or sync command; using "
              "write-through\n",
              unit->lun);
      result = 0;
    }
    work->success = result == 0;
    return;
  }
  if (work->operation == STORAGE_READ) {
    work->success = storage_blocks(unit, work->block, work->blocks, work->bytes,
                                   true, work->error);
    return;
  }
  /* The public disk API uses 512-byte sectors. Preserve neighboring bytes on
   * media with larger logical blocks, under the same serialized BOT owner. */
  unsigned span = work->blocks * unit->block_size;
  uint8_t *original = work->bytes + span;
  if ((work->offset != 0 || work->length != span) &&
      !storage_blocks(unit, work->block, work->blocks, work->bytes, true,
                      work->error))
    return;
  memcpy(work->bytes + work->offset, original, work->length);
  work->success = storage_blocks(unit, work->block, work->blocks, work->bytes,
                                 false, work->error);
}

static bool storage_io(char drive, uint8_t *buffer, unsigned number,
                       unsigned lba, unsigned operation) {
  unsigned index = (unsigned)(drive - 'A');
  usb_volume_t *volume = index < 26 ? usb_drives[index] : NULL;
  if (volume == NULL || !volume->unit->storage->online ||
      lba > volume->sectors || number > volume->sectors - lba ||
      number > USB_STORAGE_TRANSFER_SECTORS)
    return false;
  usb_storage_unit_t *unit = volume->unit;
  uint64_t start = (volume->first + lba) * 512;
  unsigned length = number * 512;
  unsigned offset = start % unit->block_size;
  unsigned blocks = (offset + length + unit->block_size - 1) / unit->block_size;
  unsigned span = blocks * unit->block_size;
  usb_storage_work_t *work = malloc(sizeof(*work) + span + length);
  if (work == NULL) {
    disk_report_error(drive, "USB request allocation failed: bytes=%u",
                      span + length);
    return false;
  }
  memset(work, 0, sizeof(*work));
  work->work.execute = storage_work_execute;
  work->work.destroy = storage_work_destroy;
  work->volume = volume;
  work->operation = operation;
  work->block = start / unit->block_size;
  work->blocks = blocks;
  work->offset = offset;
  work->length = length;
  if (operation == STORAGE_WRITE)
    memcpy(work->bytes + span, buffer, length);
  unit->storage->references++;
  usb_work_submit(&work->work);
  bool success = work->success;
  if (success && operation == STORAGE_READ)
    memcpy(buffer, work->bytes + offset, length);
  if (!success)
    disk_report_error(drive, "%s LBA=%u sectors=%u device-LBA=%llu: %s",
                      operation == STORAGE_READ    ? "READ"
                      : operation == STORAGE_WRITE ? "WRITE"
                                                   : "SYNC",
                      lba, number, (unsigned long long)work->block,
                      work->error[0] ? work->error
                                     : "USB request failed/cancelled");
  usb_work_release(&work->work);
  return success;
}

static bool storage_read(char drive, unsigned char *buffer, unsigned number,
                         unsigned lba) {
  return storage_io(drive, buffer, number, lba, STORAGE_READ);
}

static bool storage_write(char drive, unsigned char *buffer, unsigned number,
                          unsigned lba) {
  return storage_io(drive, buffer, number, lba, STORAGE_WRITE);
}

static bool storage_sync(char drive) {
  return storage_io(drive, NULL, 0, 0, STORAGE_SYNC);
}

static bool storage_volume(usb_storage_unit_t *unit, uint64_t first,
                           uint64_t sectors) {
  if (sectors == 0 || first >= unit->sectors ||
      sectors > unit->sectors - first || sectors > UINT64_MAX / 512)
    return false;
  usb_volume_t *volume = malloc(sizeof(*volume));
  if (volume == NULL)
    return false;
  *volume = (usb_volume_t){.unit = unit, .first = first, .sectors = sectors};
  vdisk disk = {.Read = storage_read,
                .Write = unit->read_only ? NULL : storage_write,
                .Sync = storage_sync,
                .flag = VDISK_TYPE_BLOCK,
                .size = sectors * 512,
                .owns_serialization = true,
                .max_transfer_sectors = USB_STORAGE_TRANSFER_SECTORS};
  strcpy(disk.DriveName, "usb-storage");
  for (char drive = 'C'; drive <= 'Z'; drive++) {
    if (register_vdisk_at(drive, disk) == 0)
      continue;
    volume->drive = drive;
    volume->next = unit->storage->volumes;
    unit->storage->volumes = volume;
    usb_drives[drive - 'A'] = volume;
    bool mounted = vfs_mount_disk(drive, drive);
    usb_log("usb-storage: lun=%u drive=%c sectors=%llu block=%u %s\n",
            unit->lun, drive, (unsigned long long)sectors, unit->block_size,
            mounted ? "mounted" : "unmounted");
    return true;
  }
  free(volume);
  return false;
}

static bool storage_partitions(usb_storage_unit_t *unit) {
  uint8_t *sector = malloc(unit->block_size);
  if (sector == NULL)
    return false;
  if (!storage_blocks(unit, 0, 1, sector, true, NULL)) {
    usb_log("usb-storage: lun=%u LBA0 read failed\n", unit->lun);
    free(sector);
    return false;
  }
  bool table = sector[510] == 0x55 && sector[511] == 0xaa;
  unsigned entries = 0;
  for (unsigned i = 0; table && i < 4; i++) {
    const uint8_t *entry = sector + 446 + i * 16;
    if (entry[4] == 0)
      continue;
    uint32_t first = storage_le32(entry + 8), count = storage_le32(entry + 12);
    if ((entry[0] != 0 && entry[0] != 0x80) || first == 0 || count == 0 ||
        (uint64_t)first + count > unit->sectors) {
      table = false;
      break;
    }
    entries++;
  }
  bool mounted = false;
  if (table && entries != 0) {
    for (unsigned i = 0; i < 4; i++) {
      const uint8_t *entry = sector + 446 + i * 16;
      if (entry[4] == 0)
        continue;
      if (entry[4] == 0xee || entry[4] == 5 || entry[4] == 0x0f ||
          entry[4] == 0x85) {
        usb_log("usb-storage: partition type %02x unsupported\n", entry[4]);
        continue;
      }
      mounted |= storage_volume(unit, storage_le32(entry + 8),
                                storage_le32(entry + 12));
    }
  } else {
    mounted = storage_volume(unit, 0, unit->sectors);
  }
  free(sector);
  return mounted;
}

static void storage_detach(usb_interface_t *interface) {
  usb_storage_t *storage = interface->data;
  storage->online = false;
  for (usb_volume_t *volume = storage->volumes; volume != NULL;
       volume = volume->next) {
    if (usb_drives[volume->drive - 'A'] == volume) {
      usb_drives[volume->drive - 'A'] = NULL;
      logout_vdisk(volume->drive);
      usb_log("usb-storage: drive=%c removed\n", volume->drive);
    }
  }
  storage_release(storage);
  interface->data = NULL;
  interface->detach = NULL;
}

bool usb_storage_bind(usb_interface_t *interface) {
  usb_storage_t *storage = malloc(sizeof(*storage));
  if (storage == NULL)
    return false;
  *storage = (usb_storage_t){.device = interface->device,
                             .references = 1,
                             .interface = interface->number,
                             .online = true};
  for (unsigned offset = 0; offset < interface->length;) {
    const uint8_t *entry = interface->descriptors + offset;
    if (entry[1] == USB_DESCRIPTOR_ENDPOINT &&
        (entry[3] & 3) == USB_ENDPOINT_BULK) {
      if (entry[2] & USB_DIRECTION_IN)
        storage->in = entry[2];
      else
        storage->out = entry[2];
    }
    offset += entry[0];
  }
  if (storage->in == 0 || storage->out == 0) {
    free(storage);
    return false;
  }
  interface->data = storage;
  interface->detach = storage_detach;
  uint8_t max_lun = 0;
  usb_setup_t setup = {
      .type = 0xa1, .request = 0xfe, .index = interface->number, .length = 1};
  int result = storage->device->ops->control(storage->device, &setup, &max_lun);
  if ((result != 1 && result != USB_ERROR_STALL) || max_lun > 15) {
    storage_detach(interface);
    return false;
  }
  bool active = false;
  for (unsigned lun = 0; lun <= max_lun; lun++) {
    usb_storage_unit_t *unit = malloc(sizeof(*unit));
    if (unit == NULL)
      break;
    *unit = (usb_storage_unit_t){.storage = storage, .lun = lun};
    uint8_t command[16] = {0x12, 0, 0, 0, 36, 0}, response[36];
    if (storage_scsi(unit, command, 6, response, 36, true, NULL) < 36 ||
        (response[0] & 31) != 0) {
      free(unit);
      continue;
    }
    memset(command, 0, sizeof(command));
    if (storage_scsi(unit, command, 6, NULL, 0, false, NULL) < 0) {
      free(unit);
      continue;
    }
    command[0] = 0x25;
    int length = storage_scsi(unit, command, 10, response, 8, true, NULL);
    bool capacity_valid = length == 8;
    uint64_t last = scsi_number(response, 4);
    unsigned block_size = scsi_number(response + 4, 4);
    if (length == 8 && last == UINT_MAX) {
      memset(command, 0, sizeof(command));
      command[0] = 0x9e;
      command[1] = 0x10;
      command[13] = 32;
      length = storage_scsi(unit, command, 16, response, 32, true, NULL);
      capacity_valid = length == 32;
      last = scsi_number(response, 8);
      block_size = scsi_number(response + 8, 4);
    }
    if (!capacity_valid || block_size < 512 || block_size % 512 != 0 ||
        block_size > (INT_MAX - 65536) / 3 || last == UINT64_MAX ||
        last + 1 > UINT64_MAX / block_size) {
      free(unit);
      continue;
    }
    unit->block_size = block_size;
    unit->sectors = (last + 1) * (block_size / 512);
    storage_cache_probe(unit);
    unit->next = storage->units;
    storage->units = unit;
    active |= storage_partitions(unit);
  }
  if (!active)
    storage_detach(interface);
  return active;
}
