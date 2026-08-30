#include <dosldr.h>
int getReadyDisk(); // init.c
vdisk vdisk_ctl[10];
int init_vdisk() {
  for (int i = 0; i < 10; i++) {
    vdisk_ctl[i].flag = VDISK_TYPE_NONE;
  }
  return 1;
}
int register_vdisk(vdisk vd) {
  for (int i = 0; i < 10; i++) {
    if (vdisk_ctl[i].flag == VDISK_TYPE_NONE) {
      vdisk_ctl[i] = vd; // 找到了！
      return i + ('A');  // 注册成功，返回drive
    }
  }
  printf("[vdisk]not found\n");
  return 0; // 注册失败
}
int logout_vdisk(char drive) {
  int index = drive - 'A';
  if (index < 0 || index >= 10 ||
      vdisk_ctl[index].flag == VDISK_TYPE_NONE) {
    return 0; // 失败
  }
  vdisk_ctl[index].flag = VDISK_TYPE_NONE;
  return 1;
}
int rw_vdisk(char drive, unsigned int lba, unsigned char *buffer,
             unsigned int number, int read) {
  int index = drive - 'A';
  if (index < 0 || index >= 10 ||
      vdisk_ctl[index].flag != VDISK_TYPE_BLOCK) {
    return 0;
  }
  void (*operation)(char, unsigned char *, unsigned int, unsigned int) =
      read ? vdisk_ctl[index].Read : vdisk_ctl[index].Write;
  if (operation == NULL) {
    return 0;
  }
  operation(drive, buffer, number, lba);
  return 1;
}
int get_vdisk_type(char drive) {
  int index = drive - 'A';
  return index < 0 || index >= 10 ? VDISK_TYPE_NONE
                                  : vdisk_ctl[index].flag;
}
bool have_vdisk(char drive) {
  return get_vdisk_type(drive) != VDISK_TYPE_NONE;
}
// 基于vdisk的通用读写
#define SECTORS_ONCE 8
void disk_read(unsigned int lba, unsigned int number, void *buffer,
               char drive) {
  if (DiskReady(drive)) {
    for (int i = 0; i < number; i += SECTORS_ONCE) {
      int sectors =
          ((number - i) >= SECTORS_ONCE) ? SECTORS_ONCE : (number - i);
      rw_vdisk(drive, lba + i, buffer + i * 512, sectors, 1);
    }
  }
}
int disk_Size(char drive) {
  if (!DiskReady(drive)) {
    printk("Disk Not Ready.\n");
    return 0;
  }
  return vdisk_ctl[drive - 'A'].size;
}
bool DiskReady(char drive) {
  int index = drive - 'A';
  return index >= 0 && index < 10 &&
         vdisk_ctl[index].flag == VDISK_TYPE_BLOCK &&
         vdisk_ctl[index].Read != NULL;
}
int getReadyDisk() { return 0; }
void disk_write(unsigned int lba, unsigned int number, void *buffer,
                char drive) {
  //  printf("%d\n",lba);
  if (DiskReady(drive)) {
    // printk("*buffer(%d %d) = %02x\n",lba,number,*(unsigned char *)buffer);
    for (int i = 0; i < number; i += SECTORS_ONCE) {
      int sectors =
          ((number - i) >= SECTORS_ONCE) ? SECTORS_ONCE : (number - i);
      rw_vdisk(drive, lba + i, buffer + i * 512, sectors, 0);
    }
  }
}
