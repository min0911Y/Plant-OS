#include <dos.h>

static bool dev_read(char drive, unsigned char *buffer, unsigned int number,
                     unsigned int lba) {
  (void)drive;
  (void)lba;
  if (buffer == NULL || number == 0) {
    return false;
  }
  memset(buffer, 0, number * 512);
  *(uint32_t *)buffer = 1;
  return true;
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
  disk_read(0, 1, marker, disk_number);
  bool present = *marker == 1;
  page_free(marker, 512);
  return present;
}

static int dev_mount(vfs_t *mount) {
  (void)mount;
  return VFS_OK;
}

static void dev_unmount(vfs_t *mount) { (void)mount; }

static int dev_root(vfs_t *mount, vfs_node_t *node) {
  (void)mount;
  memset(node, 0, sizeof(*node));
  node->id.value[0] = 1;
  node->type = VFS_NODE_DIRECTORY;
  node->attributes = DIR;
  return VFS_OK;
}

static int dev_lookup(vfs_t *mount, const vfs_node_t *directory,
                      const char *name, vfs_node_t *node) {
  (void)mount;
  (void)directory;
  (void)name;
  (void)node;
  return VFS_ERROR_NO_ENTRY;
}

static int dev_read_file(vfs_t *mount, const vfs_node_t *node,
                         uint32_t offset, void *buffer, uint32_t length) {
  (void)mount;
  (void)node;
  (void)offset;
  (void)buffer;
  (void)length;
  return VFS_ERROR_NO_ENTRY;
}

static int dev_iterate(vfs_t *mount, const vfs_node_t *directory,
                       uint32_t index, vfs_dir_entry_t *entry) {
  (void)mount;
  (void)directory;
  (void)index;
  (void)entry;
  return 0;
}

void init_devfs(void) {
  vdisk disk = {0};
  strcpy(disk.DriveName, "dev");
  disk.Read = dev_read;
  disk.Write = NULL;
  disk.size = 114514;
  disk.flag = VDISK_TYPE_BLOCK;
  register_vdisk_at('B', disk);

  static const vfs_filesystem_t filesystem = {
      .name = "DEVFS",
      .check = dev_check,
      .mount = dev_mount,
      .unmount = dev_unmount,
      .root = dev_root,
      .lookup = dev_lookup,
      .read = dev_read_file,
      .iterate = dev_iterate,
  };
  vfs_register_fs(&filesystem);
}
