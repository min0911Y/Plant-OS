#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

struct __directory {
  int descriptor;
  struct finfo_block *entries;
  size_t count, position;
  struct dirent current;
};

DIR *opendir(const char *path) {
  int descriptor = open(path, O_RDONLY | O_DIRECTORY);
  if (descriptor < 0)
    return NULL;
  DIR *directory = calloc(1, sizeof(*directory));
  if (!directory ||
      list_directory(path, &directory->entries, &directory->count)) {
    int error = directory ? EIO : ENOMEM;
    free(directory);
    close(descriptor);
    errno = error;
    return NULL;
  }
  directory->descriptor = descriptor;
  return directory;
}
struct dirent *readdir(DIR *directory) {
  if (!directory) {
    errno = EBADF;
    return NULL;
  }
  if (directory->position == directory->count)
    return NULL;
  const struct finfo_block *entry = directory->entries + directory->position++;
  directory->current = (struct dirent){
      .d_off = directory->position,
      .d_reclen = sizeof(struct dirent),
      .d_type = entry->type == FILE_DIRECTORY ? DT_DIR : DT_REG};
  memcpy(directory->current.d_name, entry->name, sizeof(entry->name));
  return &directory->current;
}
int closedir(DIR *directory) {
  if (!directory) {
    errno = EBADF;
    return -1;
  }
  int status = close(directory->descriptor);
  free(directory->entries);
  free(directory);
  return status;
}
void rewinddir(DIR *directory) {
  if (directory)
    directory->position = 0;
}
int dirfd(DIR *directory) {
  if (directory)
    return directory->descriptor;
  errno = EBADF;
  return -1;
}
