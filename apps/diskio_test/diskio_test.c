#include <arg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

static int cached_io_test(void) {
  const size_t length = 129 * 4096 + 19, prefix = 4097;
  unsigned char *data = malloc(length), *readback = malloc(length);
  int fd = -1, gap = -1, status = 58;
  if (!data || !readback)
    goto done;
  for (size_t i = 0; i < length; i++)
    data[i] = (unsigned char)(i * 17 + (i >> 9));
  fd = open("cache.dat", O_CREAT | O_TRUNC | O_RDWR, 0600);
  gap = open("gap.dat", O_CREAT | O_TRUNC | O_RDWR, 0600);
  /* Interleaved growth leaves a fragmented chain, even on a fresh FAT disk. */
  if (fd < 0 || gap < 0 || write(fd, data, 4096) != 4096 ||
      write(gap, data, 4096) != 4096 ||
      write(fd, data + 4096, length - 4096) != (ssize_t)(length - 4096))
    goto done;
  if (pread(fd, readback, 17, 7) != 17 || memcmp(readback, data + 7, 17) ||
      pread(fd, readback, 31, length / 2) != 31 ||
      memcmp(readback, data + length / 2, 31) ||
      pread(fd, readback, length - 13, 13) != (ssize_t)(length - 13) ||
      memcmp(readback, data + 13, length - 13))
    goto done;
  memset(data + 131071, 93, 5);
  if (pwrite(fd, data + 131071, 5, 131071) != 5 || fsync(fd) ||
      pread(fd, readback, length, 0) != (ssize_t)length ||
      memcmp(readback, data, length))
    goto done;
  /* A cached forward seek must not survive truncation and chain reuse. */
  if (ftruncate(fd, prefix) || ftruncate(fd, length))
    goto done;
  memset(data + prefix, 0, length - prefix);
  if (pread(fd, readback, length, 0) != (ssize_t)length ||
      memcmp(readback, data, length) || fsync(fd))
    goto done;
  /* Growth that overlaps EOF must keep its supplied bytes, while a later
   * write beyond EOF must zero the gap even when clusters are reused. */
  if (pwrite(fd, data, 31, length - 17) != 31 ||
      pread(fd, readback, 31, length - 17) != 31 || memcmp(readback, data, 31))
    goto done;
  const size_t hole = 8197, end = length + 14;
  if (pwrite(fd, data, 19, end + hole) != 19 || fsync(fd) ||
      pread(fd, readback, hole + 19, end) != (ssize_t)(hole + 19) ||
      memcmp(readback + hole, data, 19))
    goto done;
  for (size_t i = 0; i < hole; i++)
    if (readback[i] != 0)
      goto done;
  status = 0;
done:
  if (fd >= 0)
    close(fd);
  if (gap >= 0)
    close(gap);
  if (unlink("cache.dat") || unlink("gap.dat"))
    status = 58;
  free(data);
  free(readback);
  if (!status)
    logkf("CACHEIO PASS\n");
  return status;
}

static int long_filename_test(void) {
  const char *directory = "Long filename regression directory";
  const char *renamed = "Renamed long filename directory";
  const char *names[] = {"Mixed Case filename.txt",
                         "collision filename one.txt",
                         "collision filename two.txt",
                         "multiple.dots.in.a.filename",
                         "1234567890123",
                         "12345678901234567890123456xx",
                         "\xe4\xb8\xad\xe6\x96\x87-\xf0\x9f\x8c\xb1.txt"};
  if (mkdir(directory) != 0 || chdir(directory) != 0) {
    return 40;
  }
  const char *invalid[] = {"bad?.txt",         "trailing.",
                           "trailing ",        "\xc0\xaf.txt",
                           "\xed\xa0\x80.txt", "\xf4\x90\x80\x80.txt"};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    int fd = open(invalid[i], O_WRONLY | O_CREAT, 0);
    if (fd >= 0) {
      close(fd);
      return 56;
    }
  }
  char maximum[255];
  memset(maximum, 'x', sizeof(maximum) - 1);
  maximum[sizeof(maximum) - 1] = 0;
  for (unsigned i = 0; i <= sizeof(names) / sizeof(names[0]); i++) {
    const char *name =
        i == sizeof(names) / sizeof(names[0]) ? maximum : names[i];
    FILE *file = fopen(name, "wb");
    if (!file || fwrite(name, 1, strlen(name), file) != strlen(name) ||
        fclose(file)) {
      return 41;
    }
    char readback[255] = {0};
    file = fopen(name, "rb");
    if (!file || fread(readback, 1, sizeof(readback), file) != strlen(name) ||
        strcmp(readback, name) || fclose(file)) {
      return 42;
    }
  }
  struct stat info;
  if (stat("MIXED CASE FILENAME.TXT", &info) || stat("COLLIS~1.TXT", &info) ||
      stat("COLLIS~2.TXT", &info)) {
    return 43;
  }
  DIR *stream = opendir(".");
  if (!stream) {
    return 44;
  }
  unsigned found = 0;
  struct dirent *entry;
  while ((entry = readdir(stream))) {
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
      if (!strcmp(entry->d_name, names[i])) {
        found |= 1u << i;
      }
    }
  }
  closedir(stream);
  if (found != (1u << (sizeof(names) / sizeof(names[0]))) - 1) {
    return 45;
  }
  if (rename(names[0], "A substantially longer renamed filename.data") ||
      stat(names[0], &info) == 0 ||
      rename("A substantially longer renamed filename.data", "short.txt") ||
      unlink("short.txt")) {
    return 46;
  }
  for (unsigned i = 1; i < sizeof(names) / sizeof(names[0]); i++) {
    if (unlink(names[i])) {
      return 47;
    }
  }
  if (unlink(maximum)) {
    return 48;
  }
  /* Repeated reuse must reclaim the entire LFN sequence. */
  for (unsigned i = 0; i < 48; i++) {
    FILE *file = fopen(maximum, "wb");
    if (!file || fclose(file) || unlink(maximum)) {
      return 49;
    }
  }
  if (chdir("..") || rename(directory, renamed) || rmdir(renamed)) {
    return 50;
  }
  logkf("LFNTEST PASS\n");
  return 0;
}

static int file_size(const char *path) {
  struct stat status;
  return stat(path, &status) == 0 ? (int)status.st_size : -1;
}

static int descriptor_semantics_test(const char *path) {
  unlink(path);
  int descriptor = open(path, O_RDWR | O_CREAT | O_TRUNC, 0);
  if (descriptor < 0 || write(descriptor, "abcdef", 6) != 6 ||
      lseek(descriptor, 1, SEEK_SET) != 1) {
    return 20;
  }
  char bytes[4] = {0};
  if (read(descriptor, bytes, 2) != 2 || memcmp(bytes, "bc", 2) != 0) {
    return 21;
  }
  struct stat status;
  if (fstat(descriptor, &status) != 0 || status.st_size != 6 ||
      unlink(path) == 0) {
    return 22;
  }
  int writer = open(path, O_WRONLY);
  if (writer < 0 || lseek(writer, 1, SEEK_SET) != 1 ||
      write(writer, "Z", 1) != 1 || lseek(descriptor, 0, SEEK_SET) != 0 ||
      read(descriptor, bytes, 3) != 3 || memcmp(bytes, "aZc", 3) != 0) {
    return 23;
  }
  close(writer);
  if (lseek(descriptor, 0, SEEK_SET) != 0) {
    return 24;
  }
  int child = fork();
  if (child < 0) {
    return 25;
  }
  if (child == 0) {
    unsigned char first;
    exit(read(descriptor, &first, 1) == 1 ? first : 1);
  }
  int child_status = waittid(child);
  unsigned char second;
  if (child_status != 'a' || read(descriptor, &second, 1) != 1 ||
      second != 'Z') {
    return 26;
  }
  close(descriptor);
  if (unlink(path) != 0) {
    return 27;
  }
  return 0;
}

static int record_lock_command(int descriptor, int command, int16_t type,
                               int64_t start, int64_t length) {
  struct flock request = {.l_type = type,
                          .l_whence = SEEK_SET,
                          .l_start = start,
                          .l_len = length};
  return fcntl(descriptor, command, &request);
}

static int record_lock_conflict(int result) {
  return result == -1 && (errno == EACCES || errno == EAGAIN);
}

static int record_lock_test(const char *path) {
  unlink(path);
  int descriptor = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  char contents[128] = {0};
  if (descriptor < 0 || write(descriptor, contents, sizeof(contents)) !=
                            (ssize_t)sizeof(contents)) {
    return 60;
  }

  int ready[2], acquired[2];
  if (record_lock_command(descriptor, F_SETLK, F_WRLCK, 0, 0) ||
      pipe(ready) || pipe(acquired)) {
    close(descriptor);
    unlink(path);
    return 61;
  }
  pid_t owner = getpid();
  int child = fork();
  if (child < 0) {
    return 62;
  }
  if (child == 0) {
    close(ready[0]);
    close(acquired[0]);
    struct flock query = {.l_type = F_WRLCK,
                          .l_whence = SEEK_SET,
                          .l_start = 0,
                          .l_len = 0};
    if (fcntl(descriptor, F_GETLK, &query) || query.l_type != F_WRLCK ||
        query.l_pid != owner) {
      exit(63);
    }
    errno = 0;
    if (!record_lock_conflict(
            record_lock_command(descriptor, F_SETLK, F_WRLCK, 0, 0)) ||
        write(ready[1], "R", 1) != 1) {
      exit(64);
    }
    if (record_lock_command(descriptor, F_SETLKW, F_WRLCK, 0, 0) ||
        write(acquired[1], "A", 1) != 1 ||
        record_lock_command(descriptor, F_SETLK, F_UNLCK, 0, 0)) {
      exit(65);
    }
    exit(0);
  }
  close(ready[1]);
  close(acquired[1]);
  char byte;
  int stage = 0;
  if (read(ready[0], &byte, 1) != 1 ||
      fcntl(acquired[0], F_SETFL, O_NONBLOCK)) {
    stage = 66;
  } else {
    errno = 0;
    if (read(acquired[0], &byte, 1) != -1 || errno != EAGAIN)
      stage = 67;
  }
  if (record_lock_command(descriptor, F_SETLK, F_UNLCK, 0, 0) ||
      fcntl(acquired[0], F_SETFL, 0)) {
    stage = stage ? stage : 68;
  }
  if (!stage && read(acquired[0], &byte, 1) != 1)
    stage = 69;
  int child_status = waittid((unsigned)child);
  close(ready[0]);
  close(acquired[0]);
  if (!stage && child_status)
    stage = child_status;

  if (!stage && record_lock_command(descriptor, F_SETLK, F_RDLCK, 0, 0))
    stage = 70;
  if (!stage) {
    child = fork();
    if (child == 0) {
      int independent = open(path, O_RDWR);
      int result = independent < 0 ||
                   record_lock_command(independent, F_SETLK, F_RDLCK, 0, 0);
      errno = 0;
      result |= !record_lock_conflict(
          record_lock_command(independent, F_SETLK, F_WRLCK, 0, 0));
      if (independent >= 0)
        close(independent);
      exit(result ? 71 : 0);
    }
    if (child < 0 || waittid((unsigned)child) != 0)
      stage = 72;
    if (record_lock_command(descriptor, F_SETLK, F_UNLCK, 0, 0))
      stage = stage ? stage : 73;
  }

  if (!stage &&
      (record_lock_command(descriptor, F_SETLK, F_WRLCK, 0, 100) ||
       record_lock_command(descriptor, F_SETLK, F_UNLCK, 20, 30))) {
    stage = 74;
  }
  if (!stage) {
    child = fork();
    if (child == 0) {
      int independent = open(path, O_RDWR);
      int result = independent < 0 ||
                   record_lock_command(independent, F_SETLK, F_WRLCK, 20, 30);
      errno = 0;
      result |= !record_lock_conflict(
          record_lock_command(independent, F_SETLK, F_WRLCK, 0, 20));
      if (independent >= 0)
        close(independent);
      exit(result ? 75 : 0);
    }
    if (child < 0 || waittid((unsigned)child) != 0)
      stage = 76;
    if (record_lock_command(descriptor, F_SETLK, F_UNLCK, 0, 0))
      stage = stage ? stage : 77;
  }

  int other = -1;
  int release[2];
  if (!stage) {
    other = open(path, O_RDWR);
    if (other < 0 || pipe(release) ||
        record_lock_command(descriptor, F_SETLK, F_WRLCK, 0, 0)) {
      stage = 78;
    }
  }
  if (!stage) {
    child = fork();
    if (child == 0) {
      close(release[1]);
      int independent = open(path, O_RDWR);
      int result = read(release[0], &byte, 1) != 1 || independent < 0 ||
                   record_lock_command(independent, F_SETLK, F_WRLCK, 0, 0);
      if (independent >= 0)
        close(independent);
      exit(result ? 79 : 0);
    }
    close(release[0]);
    if (child < 0 || close(other) || write(release[1], "C", 1) != 1 ||
        waittid((unsigned)child) != 0) {
      stage = 80;
    }
    close(release[1]);
    other = -1;
  }
  if (other >= 0)
    close(other);
  record_lock_command(descriptor, F_SETLK, F_UNLCK, 0, 0);

  int held[2];
  if (!stage && pipe(held))
    stage = 81;
  if (!stage) {
    child = fork();
    if (child == 0) {
      close(held[0]);
      int independent = open(path, O_RDWR);
      int result = independent < 0 ||
                   record_lock_command(independent, F_SETLK, F_WRLCK, 0, 0) ||
                   write(held[1], "X", 1) != 1;
      exit(result ? 82 : 0);
    }
    close(held[1]);
    if (child < 0 || read(held[0], &byte, 1) != 1 ||
        waittid((unsigned)child) != 0 ||
        record_lock_command(descriptor, F_SETLK, F_WRLCK, 0, 0)) {
      stage = 83;
    }
    close(held[0]);
  }

  record_lock_command(descriptor, F_SETLK, F_UNLCK, 0, 0);
  close(descriptor);
  if (unlink(path))
    stage = stage ? stage : 84;
  if (!stage)
    logkf("RECORD_LOCK PASS\n");
  return stage;
}

static void write_pattern_file(const char *path,
                               const char *tag,
                               int rounds,
                               int chunk_size) {
  FILE *fp = fopen((char *)path, "w");
  if (!fp) {
    printf("open failed: %s\n", path);
    exit(2);
  }
  char *buf = malloc(chunk_size + 64);
  if (!buf) {
    printf("alloc failed\n");
    fclose(fp);
    exit(3);
  }
  for (int i = 0; i < rounds; i++) {
    int len = snprintf(buf, chunk_size + 64, "%s round=%d tid=%d\n", tag, i,
                       NowTaskID());
    while (len < chunk_size) {
      buf[len++] = 'A' + (i % 26);
    }
    fwrite(buf, 1, len, fp);
    api_yield();
  }
  free(buf);
  fclose(fp);
}

static void read_back(const char *path) {
  int sz = file_size(path);
  if (sz < 0) {
    printf("read check failed: %s missing\n", path);
    exit(4);
  }
  char *buf = malloc(sz + 1);
  if (!buf) {
    printf("alloc failed\n");
    exit(5);
  }
  FILE *stream = fopen(path, "rb");
  if (stream == NULL || fread(buf, 1, sz, stream) != (size_t)sz) {
    printf("read failed: %s\n", path);
    free(buf);
    exit(6);
  }
  fclose(stream);
  buf[sz] = 0;
  printf("%s size=%d head=%.24s\n", path, sz, buf);
  free(buf);
}

static int delayed_wait_reuse_test(void) {
  int pid = fork();
  if (pid < 0) {
    printf("reuse fork failed\n");
    return 10;
  }
  if (pid == 0) {
    exit(23);
  }
  sleep(100);
  int status = waittid(pid);
  printf("dktest reuse_tid=%d status=%d\n", pid, status);
  return status == 23 ? 0 : 11;
}

static int orphan_survival_test(const char *path) {
  int parent = fork();
  if (parent < 0) {
    printf("orphan parent fork failed\n");
    return 12;
  }
  if (parent == 0) {
    int worker = fork();
    if (worker < 0) {
      exit(13);
    }
    if (worker == 0) {
      sleep(50);
      write_pattern_file(path, "orphan", 4, 128);
      exit(24);
    }
    exit(37);
  }

  sleep(100);
  int status = waittid(parent);
  if (status != 37) {
    printf("orphan parent status=%d\n", status);
    return 14;
  }
  sleep(200);
  if (file_size(path) < 0) {
    printf("orphan child was killed with its parent\n");
    return 15;
  }
  read_back(path);
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "--lfn")) {
    if (argc == 3 && chdir(argv[2])) {
      return 39;
    }
    if (argc == 3) {
      struct stat info;
      if (stat("Damaged long filename.txt", &info) == 0 ||
          stat("DAMAGE~1.TXT", &info) || info.st_size != 12) {
        return 55;
      }
      FILE *file = fopen("External long filename from mtools.txt", "rb");
      char data[16] = {0};
      if (!file || fread(data, 1, 16, file) != 12 || fclose(file) ||
          strcmp(data, "mtools seed\n")) {
        return 51;
      }
      file = fopen("External empty long filename.txt", "wb");
      if (!file || fwrite("Plant LFN\n", 1, 10, file) != 10 || fclose(file) ||
          rename("External empty long filename.txt",
                 "Exported long filename from Plant.txt")) {
        return 52;
      }
    }
    int status = long_filename_test();
    if (!status)
      status = cached_io_test();
    if (!status && argc == 3) {
      uint8_t drive = argv[2][0];
      if (chdir("R:") || !vfs_unmount_disk(drive) || !vfs_mount(drive, drive) ||
          chdir(argv[2])) {
        return 53;
      }
      FILE *file = fopen("Exported long filename from Plant.txt", "rb");
      char data[16] = {0};
      if (!file || fread(data, 1, 16, file) != 10 || fclose(file) ||
          strcmp(data, "Plant LFN\n")) {
        return 54;
      }
    }
    if (status) {
      logkf("LFNTEST FAIL stage=%d errno=%d\n", status, errno);
    }
    return status;
  }
  char parent_path[64] = "parent.log";
  char child_path[64] = "child.log";
  char orphan_path[64] = "orphan.log";
  char descriptor_path[64] = "fdtest.bin";
  char record_lock_path[64] = "record-lock.bin";
  if (argc == 2) {
    snprintf(parent_path, sizeof(parent_path), "%s/parent.log", argv[1]);
    snprintf(child_path, sizeof(child_path), "%s/child.log", argv[1]);
    snprintf(orphan_path, sizeof(orphan_path), "%s/orphan.log", argv[1]);
    snprintf(descriptor_path, sizeof(descriptor_path), "%s/fdtest.bin",
             argv[1]);
    snprintf(record_lock_path, sizeof(record_lock_path), "%s/record-lock.bin",
             argv[1]);
  }
  const int rounds = 64;
  const int chunk_size = 512;

  int descriptor_status = descriptor_semantics_test(descriptor_path);
  if (descriptor_status != 0) {
    logkf("DKTEST descriptor failure=%d target=%s\n", descriptor_status,
          descriptor_path);
    return descriptor_status;
  }
  int record_lock_status = record_lock_test(record_lock_path);
  if (record_lock_status != 0) {
    logkf("DKTEST record lock failure=%d errno=%d target=%s\n",
          record_lock_status, errno, record_lock_path);
    return record_lock_status;
  }

  if (file_size(parent_path) != -1) {
    unlink(parent_path);
  }
  if (file_size(child_path) != -1) {
    unlink(child_path);
  }
  if (file_size(orphan_path) != -1) {
    unlink(orphan_path);
  }

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    return 1;
  }

  if (pid == 0) {
    write_pattern_file(child_path, "child", rounds, chunk_size);
    read_back(child_path);
    exit(0);
  }

  write_pattern_file(parent_path, "parent", rounds, chunk_size);
  sleep(100);
  int status = waittid(pid);
  read_back(parent_path);
  read_back(child_path);
  printf("dktest child_status=%d\n", status);
  if (status != 0) {
    return 7;
  }
  status = delayed_wait_reuse_test();
  if (status != 0) {
    return status;
  }
  status = orphan_survival_test(orphan_path);
  printf("dktest lifecycle_status=%d\n", status);
  if (status == 0) {
    logkf("DKTEST PASS target=%s\n", argc == 2 ? argv[1] : ".");
  }
  return status;
}
