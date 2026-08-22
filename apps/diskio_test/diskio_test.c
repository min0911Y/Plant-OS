#include <arg.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

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
  int sz = filesize((char *)path);
  if (sz < 0) {
    printf("read check failed: %s missing\n", path);
    exit(4);
  }
  char *buf = malloc(sz + 1);
  if (!buf) {
    printf("alloc failed\n");
    exit(5);
  }
  if (!api_ReadFile((char *)path, buf)) {
    printf("read failed: %s\n", path);
    free(buf);
    exit(6);
  }
  buf[sz] = 0;
  printf("%s size=%d head=%.24s\n", path, sz, buf);
  free(buf);
}

int main(void) {
  const char *parent_path = "parent.log";
  const char *child_path = "child.log";
  const int rounds = 64;
  const int chunk_size = 512;

  if (filesize((char *)parent_path) != -1) {
    vfs_delfile((char *)parent_path);
  }
  if (filesize((char *)child_path) != -1) {
    vfs_delfile((char *)child_path);
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
  int status = waittid(pid);
  read_back(parent_path);
  read_back(child_path);
  printf("dktest child_status=%d\n", status);
  return status;
}
