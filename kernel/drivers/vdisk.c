#include <dos.h>
#include <drivers.h>
int getReadyDisk(); // init.c
vdisk vdisk_ctl[26];
static unsigned char *drive_name[16] = {NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL};
static struct FIFO8 drive_fifo[16];
static unsigned char drive_buf[16][256];

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
  while (drive_buf[drive_code][drive_fifo[drive_code].q] != get_tid(current_task())) {
    task_fall_blocked_reason(WAITING, WAIT_REASON_DISK);
  }
  return true;
}
int init_vdisk() {
  for (int i = 0; i < 26; i++) {
    vdisk_ctl[i].flag = 0; // 设置为未使用
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
  if (indx > 26) {
    return 0; // 失败
  }
  if (vdisk_ctl[indx].flag) {
    vdisk_ctl[indx].flag = 0; // 设置为没有
    return 1;                 // 成功
  } else {
    return 0; // 失败
  }
}
int rw_vdisk(char drive, unsigned int lba, unsigned char *buffer,
             unsigned int number, int read) {
  int indx = drive - ('A');
  if (indx > 26) {
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
  if (indx > 26) {
    return 0; // 失败
  }
  if (vdisk_ctl[indx].flag) {
    return 1; // 成功
  } else {
    return 0; // 失败
  }
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
    if (drive_name[i] == NULL) {
      drive_name[i] = name;
      fifo8_init(&drive_fifo[i], 256, drive_buf[i]);
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
  fifo8_put(&drive_fifo[drive_code], get_tid(current_task()));
  return drive_wait_turn(drive_code);
}
void DriveSemaphoreGive(unsigned int drive_code) {
  if (drive_code >= 16) {
    return;
  }
  if (drive_can_skip_sync()) {
    return;
  }
  if (drive_buf[drive_code][drive_fifo[drive_code].q] != get_tid(current_task())) {
    // 暂时先不做处理 一般不会出现这种情况
    return;
  }
  fifo8_get(&drive_fifo[drive_code]);
  if (fifo8_status(&drive_fifo[drive_code]) > 0) {
    mtask *next = get_task(drive_buf[drive_code][drive_fifo[drive_code].q]);
    if (next) {
      mtask_run_now(next);
      task_run(next);
    }
  }
}

void vdisk_remove_task(unsigned tid) {
  for (unsigned int drive_code = 0; drive_code < 16; drive_code++) {
    struct FIFO8 *fifo = &drive_fifo[drive_code];
    unsigned char keep[256];
    int keep_count = 0;
    int count = fifo8_status(fifo);
    bool removed = false;

    for (int i = 0; i < count; i++) {
      int queued_tid = fifo8_get(fifo);
      if ((unsigned)queued_tid == tid) {
        removed = true;
        continue;
      }
      keep[keep_count++] = (unsigned char)queued_tid;
    }
    for (int i = 0; i < keep_count; i++) {
      fifo8_put(fifo, keep[i]);
    }
    if (removed && fifo8_status(fifo) > 0) {
      mtask *next = get_task(drive_buf[drive_code][fifo->q]);
      task_run(next);
    }
  }
}

#define SECTORS_ONCE 8
void Disk_Read(unsigned int lba, unsigned int number, void *buffer,
               char drive) {
  if (have_vdisk(drive)) {
    unsigned int drive_code = disk_drive_slot(drive);
    if (DriveSemaphoreTake(drive_code)) {
    for (int i = 0; i < number; i += SECTORS_ONCE) {
      int sectors = ((number - i) >= SECTORS_ONCE) ? SECTORS_ONCE : (number - i);
        rw_vdisk(drive, lba + i, buffer + i * 512, sectors, 1);
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
void Disk_Write(unsigned int lba, unsigned int number, void *buffer,
                char drive) {
//  printk("%d\n",lba);
  if (have_vdisk(drive)) {
    unsigned int drive_code = disk_drive_slot(drive);
    if (DriveSemaphoreTake(drive_code)) {
     // printk("*buffer(%d %d) = %02x\n",lba,number,*(unsigned char *)buffer);
    for (int i = 0; i < number; i += SECTORS_ONCE) {
      int sectors = ((number - i) >= SECTORS_ONCE) ? SECTORS_ONCE : (number - i);
        rw_vdisk(drive, lba + i, buffer + i * 512, sectors, 0);
      }
      DriveSemaphoreGive(drive_code);
    }
  }
}
bool CDROM_Read(unsigned int lba, unsigned int number, void *buffer,
                char drive) {
  if (have_vdisk(drive)) {
    int indx = drive - ('A');
    if(vdisk_ctl[indx].flag != 2) {
      return false;
    }
    unsigned int drive_code = disk_drive_slot(drive);
    if (DriveSemaphoreTake(drive_code)) {
      for (int i = 0; i < number; i += SECTORS_ONCE) {
        int sectors = ((number - i) >= SECTORS_ONCE) ? SECTORS_ONCE : (number - i);
        rw_vdisk(drive, lba + i, buffer + i * 2048, sectors, 1);
      }
      DriveSemaphoreGive(drive_code);
    }
    return  true;
  }
  return false;
}
