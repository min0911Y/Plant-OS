#include <arg.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>

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
  char parent_path[64] = "parent.log";
  char child_path[64] = "child.log";
  char orphan_path[64] = "orphan.log";
  char descriptor_path[64] = "fdtest.bin";
  if (argc == 2) {
    snprintf(parent_path, sizeof(parent_path), "%s/parent.log", argv[1]);
    snprintf(child_path, sizeof(child_path), "%s/child.log", argv[1]);
    snprintf(orphan_path, sizeof(orphan_path), "%s/orphan.log", argv[1]);
    snprintf(descriptor_path, sizeof(descriptor_path), "%s/fdtest.bin",
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
