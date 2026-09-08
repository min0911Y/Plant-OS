#include "../third_party/musl/scan.h"
#include "stdio_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int vsscanf(const char *text, const char *format, va_list arguments) {
  scan_input_t input = {.string = text};
  return plant_scan(&input, format, arguments);
}
int vfscanf(FILE *stream, const char *format, va_list arguments) {
  scan_input_t input = {.file = stream};
  stdio_stream_lock(stream);
  int result = plant_scan(&input, format, arguments);
  stdio_stream_unlock(stream);
  return result;
}
int fscanf(FILE *stream, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vfscanf(stream, format, arguments);
  va_end(arguments);
  return result;
}
int vscanf(const char *format, va_list arguments) {
  return vfscanf(stdin, format, arguments);
}
int scanf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vscanf(format, arguments);
  va_end(arguments);
  return result;
}
int getchar(void) { return fgetc(stdin); }
int fgetpos(FILE *stream, fpos_t *position) {
  if (!position) {
    errno = EINVAL;
    return -1;
  }
  long offset = ftell(stream);
  if (offset < 0)
    return -1;
  *position = offset;
  return 0;
}
int fsetpos(FILE *stream, const fpos_t *position) {
  if (!position) {
    errno = EINVAL;
    return -1;
  }
  return fseek(stream, *position, SEEK_SET);
}

int vasprintf(char **text, const char *format, va_list arguments) {
  if (!text || !format) {
    errno = EINVAL;
    return -1;
  }
  *text = NULL;
  va_list copy;
  va_copy(copy, arguments);
  int count = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (count < 0)
    return -1;
  char *buffer = malloc((size_t)count + 1);
  if (!buffer)
    return -1;
  int written = vsnprintf(buffer, (size_t)count + 1, format, arguments);
  if (written != count) {
    free(buffer);
    return -1;
  }
  *text = buffer;
  return count;
}
int asprintf(char **text, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vasprintf(text, format, arguments);
  va_end(arguments);
  return result;
}
