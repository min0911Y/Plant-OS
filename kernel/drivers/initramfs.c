#include <boot.h>
#include <drivers.h>
#include <string.h>

enum {
  INITRAMFS_SECTOR_SIZE = 512,
  INITRAMFS_BOOT_SIGNATURE_OFFSET = 510,
  INITRAMFS_TOTAL_SECTORS16_OFFSET = 19,
  INITRAMFS_TOTAL_SECTORS32_OFFSET = 32,
  INITRAMFS_BYTES_PER_SECTOR_OFFSET = 11,
  INITRAMFS_MAX_TRANSFER_SECTORS = 128,
};

static boot_module_t initramfs;

static bool initramfs_span(unsigned int lba, unsigned int sectors,
                           uint32_t *offset, uint32_t *size) {
  uint32_t total_sectors = initramfs.size / INITRAMFS_SECTOR_SIZE;
  if (lba > total_sectors || sectors > total_sectors - lba) {
    return false;
  }
  *offset = lba * INITRAMFS_SECTOR_SIZE;
  *size = sectors * INITRAMFS_SECTOR_SIZE;
  return true;
}

static void initramfs_read(char drive, unsigned char *buffer,
                           unsigned int sectors, unsigned int lba) {
  (void)drive;
  uint32_t offset;
  uint32_t size;
  if (initramfs_span(lba, sectors, &offset, &size)) {
    memcpy(buffer, (const void *)(initramfs.address + offset), size);
  }
}

static void initramfs_write(char drive, unsigned char *buffer,
                            unsigned int sectors, unsigned int lba) {
  (void)drive;
  uint32_t offset;
  uint32_t size;
  if (initramfs_span(lba, sectors, &offset, &size)) {
    memcpy((void *)(initramfs.address + offset), buffer, size);
  }
}

bool boot_initramfs_register(const boot_module_t *module) {
  if (module == NULL || module->size < INITRAMFS_SECTOR_SIZE ||
      module->size % INITRAMFS_SECTOR_SIZE != 0) {
    return false;
  }

  const uint8_t *boot_sector = (const uint8_t *)module->address;
  uint16_t bytes_per_sector =
      *(const uint16_t *)(boot_sector + INITRAMFS_BYTES_PER_SECTOR_OFFSET);
  uint32_t filesystem_sectors = *(const uint16_t *)(
      boot_sector + INITRAMFS_TOTAL_SECTORS16_OFFSET);
  if (filesystem_sectors == 0) {
    filesystem_sectors = *(const uint32_t *)(
        boot_sector + INITRAMFS_TOTAL_SECTORS32_OFFSET);
  }
  if (bytes_per_sector != INITRAMFS_SECTOR_SIZE || filesystem_sectors == 0 ||
      filesystem_sectors > module->size / INITRAMFS_SECTOR_SIZE ||
      *(const uint16_t *)(boot_sector + INITRAMFS_BOOT_SIGNATURE_OFFSET) !=
          0xaa55u) {
    return false;
  }

  initramfs = *module;
  vdisk disk = {
      .Read = initramfs_read,
      .Write = initramfs_write,
      .flag = VDISK_TYPE_BLOCK,
      .size = initramfs.size,
      .max_transfer_sectors = INITRAMFS_MAX_TRANSFER_SECTORS,
  };
  strcpy(disk.DriveName, "initramfs");
  return register_vdisk_at(BOOT_INITRAMFS_DRIVE, disk) != 0;
}
