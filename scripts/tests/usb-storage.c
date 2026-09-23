/* Host BOT device fixture; compile the production driver unchanged:
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *   -idirafter kernel/include scripts/tests/usb-storage.c -o /tmp/usb-storage
 * /tmp/usb-storage
 */
#include "../../apps/include/disk_info.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usb.h>

/* Replace only the kernel services. USB structures and all BOT, SCSI, cache
 * discovery and volume I/O code below come from the production source. */
#define _DOS_H
enum { VDISK_TYPE_BLOCK = 1 };
typedef struct {
  bool (*Read)(char, unsigned char *, unsigned, unsigned);
  bool (*Write)(char, unsigned char *, unsigned, unsigned);
  unsigned flag;
  uint64_t size;
  bool (*Sync)(char);
  bool owns_serialization;
  unsigned max_transfer_sectors;
  char DriveName[50];
} vdisk;
static int register_vdisk_at(char drive, vdisk disk) {
  (void)disk;
  return drive;
}
static bool vfs_mount_disk(char disk, char drive) { return disk == drive; }
static int logout_vdisk(char drive) { return drive; }
static char reported[DISK_INFO_ERROR_SIZE];
static void disk_report_error(char drive, const char *format, ...) {
  (void)drive;
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(reported, sizeof(reported), format, arguments);
  va_end(arguments);
}
static uint64_t monotonic_time_ns(void) { return 0; }
static void sleep(unsigned long long milliseconds) {
  (void)milliseconds;
  abort();
}
void usb_log(const char *format, ...) { (void)format; }
bool usb_connected(const usb_device_t *device) { return device->connected; }
void usb_work_submit(usb_work_t *work) { work->execute(work); }
void usb_work_release(usb_work_t *work) { work->destroy(work); }

#include "../../kernel/drivers/usb_storage.c"

enum {
  MODE_WCE_OFF,
  MODE_WCE_ON,
  MODE_ABSENT,
  MODE_UNSUPPORTED,
  MODE_TRANSPORT,
  MODE_TRUNCATED,
  MODE_SHORT_PAGE,
  MODE_BAD_DESCRIPTOR,
  MODE_SUBPAGE,
  MODE_TEN,
  MODE_READ_ONLY,
};
static unsigned mode, sync_key, sync_asc, sync_ascq, sync_count, writes;
static unsigned last_key, last_asc, last_ascq, phase, actual;
static bool descriptor_sense, fail_sync_transport, fail_write;
static usb_cbw_t pending;

static int transfer(usb_device_t *device, uint8_t endpoint, void *data,
                    unsigned length) {
  (void)device;
  if (phase == 0) {
    assert(endpoint == 2 && length == sizeof(pending));
    memcpy(&pending, data, length);
    assert(pending.signature == USB_BOT_CBW_SIGNATURE);
    phase = pending.length ? 1 : 2;
    actual = 0;
    if (pending.command[0] != 3)
      last_key = last_asc = last_ascq = 0;
    if (pending.command[0] == 0x35) {
      sync_count++;
      last_key = sync_key;
      last_asc = sync_asc;
      last_ascq = sync_ascq;
      if (fail_sync_transport) {
        phase = 0;
        return USB_ERROR_IO;
      }
    }
    return length;
  }
  if (phase == 2) {
    assert(endpoint == 0x81 && length == sizeof(usb_csw_t));
    usb_csw_t csw = {.signature = USB_BOT_CSW_SIGNATURE,
                     .tag = pending.tag,
                     .residue = pending.length - actual,
                     .status = pending.command[0] != 3 && last_key ? 1 : 0};
    memcpy(data, &csw, length);
    phase = 0;
    return length;
  }
  assert(length == pending.length);
  phase = 2;
  if (pending.command[0] == 0x2a) {
    assert(endpoint == 2);
    writes++;
    if (fail_write) {
      last_key = 3;
      last_asc = 0x0c;
      return USB_ERROR_STALL;
    }
    actual = length;
    return length;
  }
  assert(endpoint == 0x81);
  uint8_t *bytes = data;
  memset(bytes, 0, length);
  switch (pending.command[0]) {
  case 3:
    assert(length == 18);
    bytes[0] = descriptor_sense ? 0x72 : 0x70;
    bytes[descriptor_sense ? 1 : 2] = last_key;
    bytes[descriptor_sense ? 2 : 12] = last_asc;
    bytes[descriptor_sense ? 3 : 13] = last_ascq;
    bytes[7] = descriptor_sense ? 0 : 10;
    break;
  case 0x1a:
  case 0x5a: {
    if (mode == MODE_TRANSPORT) {
      phase = 0;
      return USB_ERROR_IO;
    }
    if (mode == MODE_UNSUPPORTED ||
        (mode == MODE_TEN && pending.command[0] == 0x1a)) {
      last_key = 5;
      last_asc = 0x20;
      return USB_ERROR_STALL;
    }
    unsigned header = pending.command[0] == 0x1a ? 4 : 8;
    unsigned offset = header + 8; /* A real block descriptor must be skipped. */
    bytes[header == 4 ? 3 : 7] = 8;
    if (mode == MODE_SUBPAGE) {
      bytes[offset] = 0x48;
      bytes[offset + 1] = 1;
      bytes[offset + 3] = 4;
      offset += 8;
    }
    if (mode != MODE_ABSENT) {
      bytes[offset] = 8;
      bytes[offset + 1] = mode == MODE_SHORT_PAGE ? 0 : 18;
      bytes[offset + 2] =
          mode == MODE_WCE_OFF || mode == MODE_READ_ONLY ? 0 : 4;
      offset += 20;
    }
    if (mode == MODE_READ_ONLY)
      bytes[header == 4 ? 2 : 3] = 0x80;
    if (mode == MODE_BAD_DESCRIPTOR)
      bytes[header == 4 ? 3 : 7] = 254;
    bytes[header == 4 ? 0 : 1] = offset - (header == 4 ? 1 : 2);
    actual = mode == MODE_TRUNCATED ? offset - 1 : offset;
    return actual;
  }
  default:
    assert(!"unexpected SCSI command");
  }
  actual = length;
  return length;
}
static int control(usb_device_t *device, const usb_setup_t *setup, void *data) {
  (void)device;
  (void)data;
  assert(setup->request == 0xff);
  phase = 0;
  return 0;
}
static bool clear_halt(usb_device_t *device, uint8_t endpoint) {
  (void)device;
  (void)endpoint;
  return true;
}
static const usb_host_ops_t ops = {
    .transfer = transfer, .control = control, .clear_halt = clear_halt};

static void scenario(unsigned policy, unsigned key, unsigned asc, unsigned ascq,
                     bool success, unsigned expected_syncs) {
  mode = policy;
  sync_key = key;
  sync_asc = asc;
  sync_ascq = ascq;
  sync_count = writes = phase = 0;
  reported[0] = 0;
  usb_device_t device = {.ops = &ops, .connected = true};
  usb_storage_t storage = {
      .device = &device, .online = true, .references = 1, .in = 0x81, .out = 2};
  usb_storage_unit_t unit = {.storage = &storage, .block_size = 512};
  usb_volume_t volume = {.unit = &unit, .drive = 'C', .sectors = 8192};
  storage_cache_probe(&unit);
  assert(unit.read_only == (mode == MODE_READ_ONLY));
  usb_drives['C' - 'A'] = &volume;
  assert(storage_sync('C') == success);
  assert(storage_sync('C') == success);
  assert(sync_count == expected_syncs);
  assert((reported[0] == 0) == success);
  if (success) {
    uint8_t buffer[512] = {0};
    assert(storage_write('C', buffer, 1, 0));
    fail_write = true;
    assert(!storage_write('C', buffer, 1, 1));
    assert(strstr(reported, "WRITE") && strstr(reported, "sense=3 asc=0c/00"));
    fail_write = false;
    assert(writes == 2);
  }
  assert(storage.references == 1 && phase == 0);
  usb_drives['C' - 'A'] = NULL;
}

int main(void) {
  for (descriptor_sense = 0; descriptor_sense <= 1; descriptor_sense++) {
    scenario(MODE_WCE_OFF, 5, 0x20, 0, true, 0);
    scenario(MODE_READ_ONLY, 5, 0x20, 0, true, 0);
    scenario(MODE_ABSENT, 5, 0x20, 0, true, 1);
    scenario(MODE_UNSUPPORTED, 5, 0x20, 0, true, 1);
    scenario(MODE_WCE_ON, 0, 0, 0, true, 2);
    scenario(MODE_TEN, 0, 0, 0, true, 2);
    scenario(MODE_SUBPAGE, 5, 0x20, 0, false, 2);
    scenario(MODE_WCE_ON, 5, 0x20, 0, false, 2);
    scenario(MODE_TRANSPORT, 5, 0x20, 0, false, 2);
    scenario(MODE_TRUNCATED, 5, 0x20, 0, false, 2);
    scenario(MODE_SHORT_PAGE, 5, 0x20, 0, false, 2);
    scenario(MODE_BAD_DESCRIPTOR, 5, 0x20, 0, false, 2);
    scenario(MODE_ABSENT, 5, 0x24, 0, false, 2);
    scenario(MODE_ABSENT, 5, 0x20, 1, false, 2);
    scenario(MODE_ABSENT, 3, 0x0c, 0, false, 2);
    scenario(MODE_WCE_ON, 4, 0x44, 0, false, 2);
    fail_sync_transport = true;
    scenario(MODE_ABSENT, 0, 0, 0, false, 2);
    fail_sync_transport = false;
  }
  puts("USB storage PASS: caching modes, fixed/descriptor sense, unsupported "
       "sync, transport/media failures");
  return 0;
}
