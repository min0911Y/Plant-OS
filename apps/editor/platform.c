#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <syscall.h>

static bool terminal_active;

bool pleditor_platform_init(void) {
  int rows, columns;
  if (!pleditor_platform_get_size(&rows, &columns))
    return false;
  terminal_active = true;
  static const char enter[] = "\033[?1049h\033[0m\033[2J\033[H";
  pleditor_platform_write(enter, sizeof(enter) - 1);
  return true;
}

void pleditor_platform_cleanup(void) {
  if (!terminal_active)
    return;
  terminal_active = false;
  static const char leave[] = "\033[0m\033[?25h\033[?1049l";
  pleditor_platform_write(leave, sizeof(leave) - 1);
}

bool pleditor_platform_get_size(int *rows, int *columns) {
  *rows = tty_get_ysize();
  *columns = tty_get_xsize();
  return *rows >= 3 && *columns > 0;
}

int pleditor_platform_read_key(void) {
  for (;;) {
    int key = getch();
    switch (key) {
    case KEY_INPUT_UP:
      return PLEDITOR_ARROW_UP;
    case KEY_INPUT_DOWN:
      return PLEDITOR_ARROW_DOWN;
    case KEY_INPUT_LEFT:
      return PLEDITOR_ARROW_LEFT;
    case KEY_INPUT_RIGHT:
      return PLEDITOR_ARROW_RIGHT;
    case KEY_INPUT_HOME:
      return PLEDITOR_HOME_KEY;
    case KEY_INPUT_END:
      return PLEDITOR_END_KEY;
    case KEY_INPUT_PAGE_UP:
      return PLEDITOR_PAGE_UP;
    case KEY_INPUT_PAGE_DOWN:
      return PLEDITOR_PAGE_DOWN;
    case KEY_INPUT_DELETE:
      return PLEDITOR_DEL_KEY;
    case '\b':
      return PLEDITOR_KEY_BACKSPACE;
    default:
      if (key > 0 && key <= UCHAR_MAX)
        return key;
      break;
    }
  }
}

void pleditor_platform_write(const char *data, size_t length) {
  while (length) {
    ssize_t written = write(1, data, length);
    if (written <= 0)
      exit(1);
    data += written;
    length -= written;
  }
}

bool pleditor_platform_read_file(const char *filename, char **buffer,
                                 size_t *length) {
  *buffer = NULL;
  *length = 0;
  struct stat status;
  if (stat(filename, &status) != 0) {
    if (errno != ENOENT)
      return false;
    *buffer = malloc(1);
    if (!*buffer) {
      errno = ENOMEM;
      return false;
    }
    **buffer = 0;
    return true;
  }
  if (!S_ISREG(status.st_mode)) {
    errno = S_ISDIR(status.st_mode) ? EISDIR : EINVAL;
    return false;
  }
  if (status.st_size > INT_MAX) {
    errno = EFBIG;
    return false;
  }
  size_t size = status.st_size;
  char *data = malloc(size + 1);
  if (!data) {
    errno = ENOMEM;
    return false;
  }
  FILE *file = fopen(filename, "rb");
  if (!file) {
    free(data);
    return false;
  }
  bool ok = fread(data, 1, size, file) == size;
  int error = ferror(file) ? errno : EIO;
  if (fclose(file) != 0) {
    ok = false;
    error = errno;
  }
  if (!ok) {
    free(data);
    errno = error ? error : EIO;
    return false;
  }
  data[size] = 0;
  *buffer = data;
  *length = size;
  return true;
}

bool pleditor_platform_write_file(const char *filename, const char *buffer,
                                  size_t length) {
  FILE *file = fopen(filename, "wb");
  if (!file)
    return false;
  bool ok = fwrite(buffer, 1, length, file) == length;
  if (fflush(file) != 0)
    ok = false;
  if (fclose(file) != 0)
    ok = false;
  return ok;
}

void *pleditor_platform_reallocate(void *pointer, size_t size) {
  void *result = realloc(pointer, size ? size : 1);
  if (!result)
    pleditor_platform_error("out of memory");
  return result;
}

_Noreturn void pleditor_platform_error(const char *message) {
  pleditor_platform_cleanup();
  fprintf(stderr, "editor: %s\n", message);
  exit(1);
  __builtin_unreachable();
}
