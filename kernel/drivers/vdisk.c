#include <dos.h>
#include <drivers.h>
#include <limits.h>
int getReadyDisk(); // init.c
vdisk vdisk_ctl[26];
static char disk_errors[26][DISK_INFO_ERROR_SIZE];
static unsigned char *drive_name[16] = {NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL};
typedef struct {
  uint32_t *tids;
  uint32_t head;
  uint32_t count;
  uint32_t capacity;
} drive_wait_queue_t;

#define DRIVE_WAIT_INITIAL_CAPACITY 16u

static drive_wait_queue_t drive_queues[16];

static bool drive_queue_push(drive_wait_queue_t *queue, uint32_t tid) {
  if (queue->count == queue->capacity) {
    uint32_t capacity = queue->capacity == 0
                            ? DRIVE_WAIT_INITIAL_CAPACITY
                            : queue->capacity * 2;
    if (capacity < queue->capacity ||
        capacity > UINT_MAX / sizeof(*queue->tids)) {
      return false;
    }
    uint32_t *tids = malloc(capacity * sizeof(*tids));
    if (tids == NULL) {
      return false;
    }
    for (uint32_t i = 0; i < queue->count; i++) {
      tids[i] = queue->tids[(queue->head + i) % queue->capacity];
    }
    free(queue->tids);
    queue->tids = tids;
    queue->head = 0;
    queue->capacity = capacity;
  }
  queue->tids[(queue->head + queue->count) % queue->capacity] = tid;
  queue->count++;
  return true;
}

static uint32_t drive_queue_front(const drive_wait_queue_t *queue) {
  return queue->count == 0 ? (uint32_t)-1 : queue->tids[queue->head];
}

static uint32_t drive_queue_pop(drive_wait_queue_t *queue) {
  uint32_t tid = drive_queue_front(queue);
  if (queue->count != 0) {
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
  }
  return tid;
}

static bool drive_queue_remove(drive_wait_queue_t *queue, uint32_t tid) {
  uint32_t count = queue->count;
  bool removed = false;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t queued_tid = drive_queue_pop(queue);
    if (queued_tid == tid) {
      removed = true;
    } else {
      drive_queue_push(queue, queued_tid);
    }
  }
  return removed;
}

static bool drive_can_skip_sync(void) {
  mtask *task = current_task();
  return task == NULL || get_tid(task) == NULL_TID;
}

static unsigned int disk_drive_slot(char drive) {
  int indx = drive - 'A';
  if (indx < 0 || indx >= 26 || !vdisk_ctl[indx].flag) {
    return 16;
  }
  if (vdisk_ctl[indx].owns_serialization) {
    return 16;
  }
  if (vdisk_ctl[indx].DriveName[0] != 0) {
    unsigned int code = GetDriveCode((unsigned char *)vdisk_ctl[indx].DriveName);
    if (code < 16) {
      return code;
    }
  }
  return GetDriveCode((unsigned char *)"DISK_DRIVE");
}

static bool drive_wait_turn(unsigned int drive_code) {
  if (drive_code >= 16 || drive_can_skip_sync()) {
    return true;
  }
  while (drive_queue_front(&drive_queues[drive_code]) !=
         get_tid(current_task())) {
    task_fall_blocked_reason(WAITING, WAIT_REASON_DISK);
  }
  return true;
}
int init_vdisk() {
  for (int i = 0; i < 26; i++) {
    vdisk_ctl[i].flag = VDISK_TYPE_NONE; // 设置为未使用
  }
  return 0;
}
int register_vdisk_at(char drive, vdisk vd) {
  int index = drive - 'A';
  if (index < 0 || index >= 26 || vdisk_ctl[index].flag ||
      !vfs_disk_reusable(drive)) {
    return 0;
  }
  vdisk_ctl[index] = vd;
  memset(disk_errors[index], 0, sizeof(disk_errors[index]));
  if (vd.DriveName[0] != 0 && !vd.owns_serialization) {
    SetDrive((unsigned char *)vdisk_ctl[index].DriveName);
  }
  return drive;
}
int register_vdisk(vdisk vd) {
  for (int i = 0; i < 26; i++) {
    int drive = register_vdisk_at('A' + i, vd);
    if (drive != 0) {
      return drive;
    }
  }
  printk("[vdisk]not found\n");
  return 0; // 注册失败
}
int logout_vdisk(char drive) {
  int indx = drive - ('A');
  if (indx < 0 || indx >= 26) {
    return 0; // 失败
  }
  if (vdisk_ctl[indx].flag) {
    vdisk_ctl[indx].flag = VDISK_TYPE_NONE;
    vfs_disk_removed(drive);
    return 1;                 // 成功
  } else {
    return 0; // 失败
  }
}
int rw_vdisk(char drive, unsigned int lba, unsigned char *buffer,
             unsigned int number, int read) {
  unsigned index = (unsigned)(drive - 'A');
  if (index >= 26 || !vdisk_ctl[index].flag || disk_errors[index][0]) {
    return false;
  }
  vdisk *disk = &vdisk_ctl[index];
  unsigned unit = disk->flag == VDISK_TYPE_OPTICAL ? 2048 : 512;
  bool (*operation)(char, unsigned char *, unsigned, unsigned) =
      read ? disk->Read : disk->Write;
  if (operation == NULL || number > UINT_MAX / unit ||
      (disk->flag == VDISK_TYPE_BLOCK &&
       (uint64_t)lba + number > disk->size / unit)) {
    disk_report_error(drive, "%s rejected: LBA=%u sectors=%u capacity=%llu",
                      read ? "READ" : "WRITE", lba, number,
                      (unsigned long long)(disk->size / unit));
    return false;
  }
  bool success = operation(drive, buffer, number, lba);
  if (!success) {
    disk_report_error(drive, "%s failed: LBA=%u sectors=%u",
                      read ? "READ" : "WRITE", lba, number);
  }
  return success;
}
bool have_vdisk(char drive) {
  int indx = drive - 'A';
  // printk("drive=%c\n",drive);
  if (indx < 0 || indx >= 26) {
    return 0; // 失败
  }
  if (vdisk_ctl[indx].flag) {
    return 1; // 成功
  } else {
    return 0; // 失败
  }
}
vdisk_type_t vdisk_type(char drive) {
  int index = drive - 'A';
  return index >= 0 && index < 26 ? vdisk_ctl[index].flag : VDISK_TYPE_NONE;
}
void disk_report_error(char drive, const char *format, ...) {
  if (!have_vdisk(drive) || disk_errors[drive - 'A'][0])
    return;
  char *error = disk_errors[drive - 'A'];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(error, DISK_INFO_ERROR_SIZE, format, arguments);
  va_end(arguments);
  logk("disk %c: %s\n", drive, error);
}

bool disk_describe(char drive, disk_info_t *info) {
  if (!have_vdisk(drive))
    return false;
  const vdisk *disk = &vdisk_ctl[drive - 'A'];
  memset(info, 0, sizeof(*info));
  info->disk = drive;
  info->type =
      disk->flag == VDISK_TYPE_OPTICAL ? DISK_INFO_OPTICAL : DISK_INFO_BLOCK;
  info->capacity_bytes = disk->size;
  info->writable = disk_writable(drive);
  memcpy(info->name, disk->DriveName, sizeof(disk->DriveName));
  memcpy(info->error, disk_errors[drive - 'A'], sizeof(info->error));
  return true;
}
char first_vdisk(void) {
  for (int i = 0; i < 26; i++) {
    if (vdisk_ctl[i].flag) {
      return (char)('A' + i);
    }
  }
  return 0;
}
char next_vdisk(char drive) {
  int indx = drive >= 'A' && drive <= 'Z' ? drive - 'A' + 1 : 0;
  for (int i = indx; i < 26; i++) {
    if (vdisk_ctl[i].flag) {
      return (char)('A' + i);
    }
  }
  return 0;
}
// 基于vdisk的通用读写
bool SetDrive(unsigned char *name) {
  for (int i = 0; i != 16; i++) {
    if (drive_name[i] != NULL &&
        strcmp((char *)drive_name[i], (char *)name) == 0) {
      return true;
    }
  }
  for (int i = 0; i != 16; i++) {
    if (drive_name[i] == NULL) {
      drive_name[i] = name;
      return true;
    }
  }
  return false;
}
unsigned int GetDriveCode(unsigned char *name) {
  for (int i = 0; i != 16; i++) {
    if (drive_name[i] != NULL &&
        strcmp((char *)drive_name[i], (char *)name) == 0) {
      return i;
    }
  }
  return 16;
}

bool DriveSemaphoreTake(unsigned int drive_code) {
  if (drive_code >= 16) {
    return true;
  }
  if (drive_can_skip_sync()) {
    return true;
  }
  if (!drive_queue_push(&drive_queues[drive_code], get_tid(current_task()))) {
    return false;
  }
  return drive_wait_turn(drive_code);
}
void DriveSemaphoreGive(unsigned int drive_code) {
  if (drive_code >= 16) {
    return;
  }
  if (drive_can_skip_sync()) {
    return;
  }
  drive_wait_queue_t *queue = &drive_queues[drive_code];
  if (drive_queue_front(queue) != get_tid(current_task())) {
    // 暂时先不做处理 一般不会出现这种情况
    return;
  }
  drive_queue_pop(queue);
  if (queue->count > 0) {
    mtask *next = get_task(drive_queue_front(queue));
    if (next) {
      mtask_run_now(next);
      task_run(next);
    }
  }
}

void vdisk_remove_task(unsigned tid) {
  for (unsigned int drive_code = 0; drive_code < 16; drive_code++) {
    drive_wait_queue_t *queue = &drive_queues[drive_code];
    if (drive_queue_remove(queue, tid) && queue->count > 0) {
      mtask *next = get_task(drive_queue_front(queue));
      if (next != NULL) {
        task_run(next);
      }
    }
  }
}

#define VDISK_DEFAULT_TRANSFER_SECTORS 8u

static unsigned int disk_transfer_sectors(char drive) {
  int index = drive - 'A';
  if (index < 0 || index >= 26 ||
      vdisk_ctl[index].max_transfer_sectors == 0) {
    return VDISK_DEFAULT_TRANSFER_SECTORS;
  }
  return vdisk_ctl[index].max_transfer_sectors;
}

static bool disk_transfer(unsigned lba, unsigned number, void *buffer,
                          char drive, bool read, unsigned unit) {
  if (!DiskReady(drive) || (number != 0 && buffer == NULL) ||
      number > UINT_MAX / unit) {
    return false;
  }
  unsigned code = disk_drive_slot(drive);
  if (!DriveSemaphoreTake(code)) {
    return false;
  }
  bool success = true;
  unsigned limit = disk_transfer_sectors(drive);
  for (unsigned i = 0; i < number;) {
    unsigned sectors = number - i < limit ? number - i : limit;
    if (lba > UINT_MAX - i ||
        !rw_vdisk(drive, lba + i, (uint8_t *)buffer + i * unit, sectors,
                  read)) {
      success = false;
      break;
    }
    i += sectors;
    scheduler_preempt_if_needed();
  }
  DriveSemaphoreGive(code);
  if (!success && read) {
    memset(buffer, 0, number * unit);
  }
  return success;
}

bool disk_read(unsigned lba, unsigned number, void *buffer, char drive) {
  return vdisk_type(drive) == VDISK_TYPE_BLOCK &&
         disk_transfer(lba, number, buffer, drive, true, 512);
}
uint64_t disk_Size(char drive) {
  unsigned char drive1 = drive;
  if (have_vdisk(drive1)) {
    int indx = drive1 - 'A';
    return vdisk_ctl[indx].size;
  } else {
    logk("Disk Not Ready.\n");
    return 0;
  }

  return 0;
}
bool DiskReady(char drive) {
  return have_vdisk(drive) && !disk_errors[drive - 'A'][0];
}
int getReadyDisk() { return 0; }
bool disk_write(unsigned lba, unsigned number, void *buffer, char drive) {
  return vdisk_type(drive) == VDISK_TYPE_BLOCK &&
         disk_transfer(lba, number, buffer, drive, false, 512);
}

bool CDROM_Read(unsigned lba, unsigned number, void *buffer, char drive) {
  return vdisk_type(drive) == VDISK_TYPE_OPTICAL &&
         disk_transfer(lba, number, buffer, drive, true, 2048);
}

bool disk_sync(char drive) {
  if (!DiskReady(drive)) {
    return false;
  }
  vdisk *disk = &vdisk_ctl[drive - 'A'];
  if (disk->Sync == NULL) {
    return true;
  }
  unsigned code = disk_drive_slot(drive);
  if (!DriveSemaphoreTake(code)) {
    return false;
  }
  bool success = disk->Sync(drive);
  DriveSemaphoreGive(code);
  if (!success)
    disk_report_error(drive, "SYNC failed");
  return success;
}

bool disk_writable(char drive) {
  return DiskReady(drive) && vdisk_ctl[drive - 'A'].flag == VDISK_TYPE_BLOCK &&
         vdisk_ctl[drive - 'A'].Write != NULL;
}
