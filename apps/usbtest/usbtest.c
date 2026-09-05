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

static int check_file(char drive, bool create) {
  char path[] = "C:/usbdata.bin";
  path[0] = drive;
  unsigned char buffer[4096];
  int fd = open(path, create ? O_RDWR | O_CREAT | O_TRUNC : O_RDONLY, 0);
  if (fd < 0)
    return 1;
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

int main(void) {
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
    int result = check_file(drives[i], true);
    if (result != 0) {
      logkf("USBSTORAGE FAIL drive=%c code=%d\n", drives[i], result);
      return result;
    }
    logkf("USBSTORAGE RW PASS tag=%s drive=%c\n", tags[i], drives[i]);
  }
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
      check_file(replacement, false) != 0)
    return 13;
  lseek(old, 0, SEEK_SET);
  if (read(old, bytes, sizeof(bytes)) >= 0 || close(old) != 0)
    return 14;
  logkf("USBSTORAGE PASS: persistent writes, native blocks, disconnect, stale "
        "handle\n");
  return 0;
}
