#include <dos.h>

// do nothing
static void Read(char drive,
                 unsigned char* buffer,
                 unsigned int number,
                 unsigned int lba) {
  (void)drive;
  (void)lba;
  if (buffer == NULL || number == 0) {
    return;
  }
  memset(buffer, 0, number * 512);
  *(uint32_t *)buffer = 1;
  printk("[dev fs]don't try to read!\n");
}
static void Write(char drive,
                  unsigned char* buffer,
                  unsigned int number,
                  unsigned int lba) {
  printk("[dev fs]don't try to write!\n");
}
static bool dev_check(uint8_t disk_number) {
  if (!DiskReady(disk_number)) {
    return false;
  }
  uint32_t *marker = page_malloc(512);
  if (marker == NULL) {
    return false;
  }
  *marker = 0;
  Disk_Read(0, 1, marker, disk_number);
  bool ok = *marker == 1;
  page_free(marker, 512);
  return ok;
}

static bool dev_copy_cache(struct vfs_t *dest, struct vfs_t *src) {
  (void)dest;
  (void)src;
  return true;
}
static void dev_release_cache(struct vfs_t *vfs) { (void)vfs; }
static bool dev_init(struct vfs_t *vfs, uint8_t disk_number) {
  (void)vfs;
  (void)disk_number;
  printk("init dev fs.\n");
  return true;
}
static int dev_cd(struct vfs_t *vfs, char *dictName) {
  (void)vfs;
  (void)dictName;
  return 1;
}
// -----
void init_devfs() {
  vdisk vd;
  strcpy(vd.DriveName, "dev");
  vd.Read = Read;
  vd.size = 114514;
  vd.Write = Write;
  vd.flag = 1;
  register_vdisk(vd);
  vfs_t fs = {0};
  fs.flag = 1;
  fs.cache = NULL;
  strcpy(fs.FSName, "DEVFS");
  fs.CopyCache = dev_copy_cache;
  fs.ReleaseCache = dev_release_cache;
  fs.Format = NULL;
  fs.CreateFile = NULL;
  fs.CreateDict = NULL;
  fs.DelDict = NULL;
  fs.DelFile = NULL;
  fs.ReadFile = NULL;
  fs.WriteFile = NULL;
  fs.DeleteFs = NULL;
  fs.cd = dev_cd;
  fs.FileSize = NULL;
  fs.Check = dev_check;
  fs.ListFile = NULL;
  fs.InitFs = dev_init;
  fs.RenameFile = NULL;
  fs.Attrib = NULL;
  fs.FileInfo = NULL;
  vfs_register_fs(fs);
}
