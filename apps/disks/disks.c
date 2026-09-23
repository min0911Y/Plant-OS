#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int main(void) {
  disk_info_t *entries = NULL;
  uint32_t capacity = 0;
  int count;
  for (;;) {
    count = disk_list(entries, capacity);
    if (count < 0) {
      if (errno == EAGAIN)
        continue;
      perror("disks");
      free(entries);
      return 1;
    }
    if ((uint32_t)count <= capacity)
      break;
    disk_info_t *grown = realloc(entries, (size_t)count * sizeof(*entries));
    if (!grown) {
      perror("disks");
      free(entries);
      return 1;
    }
    entries = grown;
    capacity = count;
  }
  printf("DISK  TYPE     ACCESS  CAPACITY                 MOUNT  FILESYSTEM  "
         "NAME\n");
  for (int i = 0; i < count; i++) {
    const disk_info_t *disk = &entries[i];
    char size[64], mount[16];
    if (disk->capacity_bytes) {
      const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
      uint64_t divisor = 1;
      unsigned unit = 0;
      while (unit + 1 < sizeof(units) / sizeof(units[0]) &&
             disk->capacity_bytes / divisor >= 1024) {
        divisor *= 1024;
        unit++;
      }
      snprintf(size, sizeof(size), "%llu %s (%llu B)",
               (unsigned long long)(disk->capacity_bytes / divisor),
               units[unit], (unsigned long long)disk->capacity_bytes);
    } else {
      snprintf(size, sizeof(size), "unknown / n/a");
    }
    if (disk->mount_drive)
      snprintf(mount, sizeof(mount), "%c:/", (char)disk->mount_drive);
    else
      snprintf(mount, sizeof(mount), "unmounted");
    printf("%c     %-8s %-7s %-24s %-10s %-11s %s\n", (char)disk->disk,
           disk->type == DISK_INFO_OPTICAL ? "optical" : "block",
           disk->error[0] ? "failed" : disk->writable ? "rw" : "ro", size, mount,
           disk->filesystem[0] ? disk->filesystem : "-", disk->name);
  }
  printf("%d disk(s)\n", count);
  for (int i = 0; i < count; i++) {
    if (entries[i].error[0])
      printf("%c: I/O ERROR: %s\n", (char)entries[i].disk, entries[i].error);
  }
  free(entries);
  return 0;
}
