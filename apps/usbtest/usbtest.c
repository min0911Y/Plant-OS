#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <time.h>

static char find_volume(const char *tag) {
  char path[] = "C:/usbtag.txt";
  for (char drive = 'C'; drive <= 'Z'; drive++) {
    path[0] = drive;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
      continue;
    char found[16] = {0};
    int length = read(fd, found, sizeof(found) - 1);
    close(fd);
    if (length > 0 && strcmp(found, tag) == 0)
      return drive;
  }
  return 0;
}

static unsigned char pattern(unsigned offset) {
  return (unsigned char)((offset * 37u) ^ (offset >> 8) ^ 0x5a);
}

static int check_file(char drive, bool create, bool high) {
  char path[32];
  snprintf(path, sizeof(path), "%c:/%s", drive,
           high ? "usbhigh.bin" : "usbdata.bin");
  unsigned char buffer[32768];
  int fd = open(path, create ? O_RDWR | (high ? 0 : O_CREAT | O_TRUNC) : O_RDONLY, 0);
  if (fd < 0)
    return 1;
  if (create && high) {
    if (read(fd, buffer, 16) != 16 ||
        memcmp(buffer, "large-fat-probe!\n", 16) || lseek(fd, 0, SEEK_SET) != 0)
      return 8;
  }
  if (create) {
    for (unsigned offset = 0; offset < 128 * 1024; offset += sizeof(buffer)) {
      for (unsigned i = 0; i < sizeof(buffer); i++)
        buffer[i] = pattern(offset + i);
      if (write(fd, buffer, sizeof(buffer)) != sizeof(buffer))
        return 2;
    }
    /* An unaligned write also checks preservation on 4 KiB media. */
    if (lseek(fd, 513, SEEK_SET) != 513 || write(fd, "USB", 3) != 3 ||
        fsync(fd) != 0)
      return 3;
  }
  if (lseek(fd, 0, SEEK_SET) != 0)
    return 4;
  for (unsigned offset = 0; offset < 128 * 1024; offset += sizeof(buffer)) {
    if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer))
      return 5;
    for (unsigned i = 0; i < sizeof(buffer); i++) {
      unsigned at = offset + i;
      unsigned char expected =
          at >= 513 && at < 516 ? "USB"[at - 513] : pattern(at);
      if (buffer[i] != expected) {
        logkf("USBSTORAGE mismatch drive=%c offset=%u\n", drive, at);
        return 6;
      }
    }
  }
  return close(fd) == 0 ? 0 : 7;
}

static bool check_disk(char disk, char mount, uint64_t bytes) {
  int count = disk_list(NULL, 0);
  if (count <= 0)
    return false;
  disk_info_t *entries = calloc(count, sizeof(*entries));
  if (!entries)
    return false;
  int actual = disk_list(entries, count);
  bool found = false;
  if (actual >= 0 && actual <= count) {
    for (int i = 0; i < actual; i++) {
      const disk_info_t *info = &entries[i];
      if (info->disk != (unsigned)disk)
        continue;
      found = info->mount_drive == (unsigned)mount &&
              info->capacity_bytes == bytes && info->type == DISK_INFO_BLOCK &&
              info->writable && !info->error[0] && !strcmp(info->name, "usb-storage") &&
              (mount ? info->filesystem[0] != 0 : info->filesystem[0] == 0);
      break;
    }
  }
  free(entries);
  return found;
}

static int check_sync_failure(const char *drives) {
  if (check_file(drives[0], true, false) != 3)
    return 20;
  disk_info_t before[26], after[26];
  int count = disk_list(before, 26);
  bool found = false;
  for (int i = 0; i < count; i++) {
    if (before[i].disk != (unsigned)drives[0])
      continue;
    found = !before[i].writable &&
            before[i].mount_drive == (unsigned)drives[0] &&
            strstr(before[i].error, "SYNC ") &&
            strstr(before[i].error, "SCSI cmd=35 sense=");
  }
  if (!found || check_file(drives[0], false, false) != 1)
    return 21;
  if (disk_list(after, 26) != count ||
      memcmp(before, after, count * sizeof(*before)))
    return 22;
  for (int i = 1; i < 3; i++) {
    if (check_file(drives[i], true, false) ||
        !check_disk(drives[i], drives[i], 131072ull * 512))
      return 23;
  }
  logkf("USBSTORAGE ERROR PASS: first sync error retained, failed volume "
        "rejects I/O, other disks healthy\n");
  return 0;
}

int main(int argc, char **argv) {
  bool sync_error = argc == 2 && !strcmp(argv[1], "sync-error");
  const char *tags[] = {"high", "super", "native4k"};
  char drives[3] = {0};
  uint64_t deadline = monotonic_ns() + 30000000000ull;
  for (unsigned i = 0; i < 3; i++) {
    while (!(drives[i] = find_volume(tags[i])) && monotonic_ns() < deadline)
      sleep(20);
    if (drives[i] == 0) {
      logkf("USBSTORAGE FAIL missing volume %s\n", tags[i]);
      return 10;
    }
    uint64_t bytes = (i == 0 ? 121075712ull : 131072ull) * 512;
    if (!check_disk(drives[i], drives[i], bytes)) {
      logkf("USBSTORAGE FAIL disk inventory\n");
      return 15;
    }
    if (sync_error)
      continue;
    int result = check_file(drives[i], true, false);
    if (result != 0) {
      logkf("USBSTORAGE FAIL drive=%c code=%d\n", drives[i], result);
      return result;
    }
    logkf("USBSTORAGE RW PASS tag=%s drive=%c\n", tags[i], drives[i]);
  }
  if (sync_error) {
    int result = check_sync_failure(drives);
    if (result)
      logkf("USBSTORAGE FAIL sync error code=%d\n", result);
    return result;
  }
  if (check_file(drives[0], true, true) != 0)
    return 17;
  if (!vfs_unmount_disk(drives[1]) ||
      !check_disk(drives[1], 0, 131072ull * 512) ||
      !vfs_mount(drives[1], 'Z') ||
      !check_disk(drives[1], 'Z', 131072ull * 512) ||
      !vfs_unmount_disk('Z') || !vfs_mount(drives[1], drives[1])) {
    logkf("USBSTORAGE FAIL unmounted/remapped disk inventory\n");
    return 16;
  }
  logkf("DISKS PASS: capacities, mounted, unmounted, alternate mount\n");
  char path[] = "C:/usbdata.bin";
  path[0] = drives[0];
  int old = open(path, O_RDWR);
  char bytes[32];
  if (old < 0 || read(old, bytes, sizeof(bytes)) != sizeof(bytes))
    return 11;
  logkf("USBSTORAGE REMOVE_READY drive=%c\n", drives[0]);
  struct stat status;
  deadline = monotonic_ns() + 15000000000ull;
  while (fstat(old, &status) == 0 && monotonic_ns() < deadline)
    sleep(20);
  lseek(old, 0, SEEK_SET);
  if (read(old, bytes, sizeof(bytes)) >= 0 || write(old, "bad", 3) >= 0)
    return 12;
  logkf("USBSTORAGE REINSERT_READY\n");
  deadline = monotonic_ns() + 15000000000ull;
  char replacement = 0;
  while (!(replacement = find_volume("high")) && monotonic_ns() < deadline)
    sleep(20);
  if (replacement == 0 || replacement == drives[0] ||
      check_file(replacement, false, false) != 0 ||
      check_file(replacement, false, true) != 0)
    return 13;
  lseek(old, 0, SEEK_SET);
  if (read(old, bytes, sizeof(bytes)) >= 0 || close(old) != 0)
    return 14;
  logkf("USBSTORAGE LARGE PASS: 57 GiB, root and file above 4 GiB\n");
  logkf("USBSTORAGE PASS: persistent writes, native blocks, disconnect, stale "
        "handle\n");
  return 0;
}
