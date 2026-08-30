#include <dos.h>
#include <drivers.h>
#include <limits.h>
int getReadyDisk(); // init.c
vdisk vdisk_ctl[26];
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
  if (index < 0 || index >= 26 || vdisk_ctl[index].flag) {
    return 0;
  }
  vdisk_ctl[index] = vd;
  if (vd.DriveName[0] != 0) {
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
    vdisk_ctl[indx].flag = VDISK_TYPE_NONE; // 设置为没有
    return 1;                 // 成功
  } else {
    return 0; // 失败
  }
}
int rw_vdisk(char drive, unsigned int lba, unsigned char *buffer,
             unsigned int number, int read) {
  int indx = drive - ('A');
  if (indx < 0 || indx >= 26) {
    return 0; // 失败
  }
  if (vdisk_ctl[indx].flag) {
    if (read) {
      vdisk_ctl[indx].Read(drive, buffer, number, lba);
    } else {
      vdisk_ctl[indx].Write(drive, buffer, number, lba);
    }
    return 1; // 成功
  } else {
    return 0; // 失败
  }
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

void disk_read(unsigned int lba, unsigned int number, void *buffer,
               char drive) {
  if (have_vdisk(drive)) {
    unsigned int drive_code = disk_drive_slot(drive);
    if (DriveSemaphoreTake(drive_code)) {
      unsigned int limit = disk_transfer_sectors(drive);
      for (unsigned int i = 0; i < number;) {
        unsigned int sectors = number - i < limit ? number - i : limit;
        rw_vdisk(drive, lba + i, (unsigned char *)buffer + i * 512, sectors,
                 1);
        i += sectors;
        scheduler_preempt_if_needed();
      }
      DriveSemaphoreGive(drive_code);
    }
  }
}
unsigned int disk_Size(char drive) {
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
bool DiskReady(char drive) { return have_vdisk(drive); }
int getReadyDisk() { return 0; }
void disk_write(unsigned int lba, unsigned int number, void *buffer,
                char drive) {
//  printk("%d\n",lba);
  if (have_vdisk(drive)) {
    unsigned int drive_code = disk_drive_slot(drive);
    if (DriveSemaphoreTake(drive_code)) {
      unsigned int limit = disk_transfer_sectors(drive);
      for (unsigned int i = 0; i < number;) {
        unsigned int sectors = number - i < limit ? number - i : limit;
        rw_vdisk(drive, lba + i, (unsigned char *)buffer + i * 512, sectors,
                 0);
        i += sectors;
        scheduler_preempt_if_needed();
      }
      DriveSemaphoreGive(drive_code);
    }
  }
}
bool CDROM_Read(unsigned int lba, unsigned int number, void *buffer,
                char drive) {
  if (have_vdisk(drive)) {
    int indx = drive - ('A');
    if(vdisk_ctl[indx].flag != VDISK_TYPE_OPTICAL) {
      return false;
    }
    unsigned int drive_code = disk_drive_slot(drive);
    if (DriveSemaphoreTake(drive_code)) {
      unsigned int limit = disk_transfer_sectors(drive);
      for (unsigned int i = 0; i < number;) {
        unsigned int sectors = number - i < limit ? number - i : limit;
        rw_vdisk(drive, lba + i, (unsigned char *)buffer + i * 2048, sectors,
                 1);
        i += sectors;
        scheduler_preempt_if_needed();
      }
      DriveSemaphoreGive(drive_code);
    }
    return  true;
  }
  return false;
}
