#include <dos.h>
#include <fs.h>
#include <limits.h>

struct FILE {
  vfs_handle_t *handle;
  uint32_t flags;
  bool eof;
  bool error;
};

static int file_mode_flags(const char *mode, uint32_t *flags) {
  if (mode == NULL || *mode == '\0') {
    return VFS_ERROR_INVALID;
  }
  uint32_t result;
  if (*mode == 'r') {
    result = VFS_OPEN_READ;
  } else if (*mode == 'w') {
    result = VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE;
  } else if (*mode == 'a') {
    result = VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_APPEND;
  } else {
    return VFS_ERROR_INVALID;
  }
  bool plus = false;
  for (mode++; *mode != '\0'; mode++) {
    if (*mode == 'b') {
      continue;
    }
    if (*mode == '+' && !plus) {
      plus = true;
      result |= VFS_OPEN_READ | VFS_OPEN_WRITE;
      continue;
    }
    return VFS_ERROR_INVALID;
  }
  *flags = result;
  return VFS_OK;
}

FILE *fopen(const char *path, const char *mode) {
  uint32_t flags;
  if (current_task() == NULL || current_task()->fs_context == NULL ||
      file_mode_flags(mode, &flags) < 0) {
    return NULL;
  }
  FILE *stream = malloc(sizeof(*stream));
  if (stream == NULL) {
    return NULL;
  }
  memset(stream, 0, sizeof(*stream));
  int status = vfs_open(current_task()->fs_context, path, flags,
                        &stream->handle);
  if (status < 0) {
    free(stream);
    return NULL;
  }
  stream->flags = flags;
  return stream;
}

int fclose(FILE *stream) {
  if (stream == NULL) {
    return EOF;
  }
  int status = vfs_close(stream->handle);
  free(stream);
  return status < 0 ? EOF : 0;
}

int fseek(FILE *stream, long offset, int whence) {
  if (stream == NULL) {
    return -1;
  }
  int position = vfs_seek(stream->handle, offset, whence);
  if (position < 0) {
    stream->error = true;
    return -1;
  }
  stream->eof = false;
  return 0;
}

long ftell(FILE *stream) {
  if (stream == NULL) {
    return -1;
  }
  int position = vfs_seek(stream->handle, 0, SEEK_CUR);
  if (position < 0) {
    stream->error = true;
  }
  return position;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
  if (stream == NULL || (stream->flags & VFS_OPEN_READ) == 0 || size == 0 ||
      count == 0) {
    return 0;
  }
  if (count > UINT_MAX / size) {
    stream->error = true;
    return 0;
  }
  uint32_t requested = size * count;
  int read = vfs_read(stream->handle, buffer, requested);
  if (read < 0) {
    stream->error = true;
    return 0;
  }
  if ((uint32_t)read < requested) {
    stream->eof = true;
  }
  return read / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
  if (stream == NULL || (stream->flags & VFS_OPEN_WRITE) == 0 || size == 0 ||
      count == 0) {
    return 0;
  }
  if (count > UINT_MAX / size) {
    stream->error = true;
    return 0;
  }
  int written = vfs_write(stream->handle, buffer, size * count);
  if (written < 0) {
    stream->error = true;
    return 0;
  }
  return written / size;
}

int fgetc(FILE *stream) {
  unsigned char value;
  return fread(&value, 1, 1, stream) == 1 ? value : EOF;
}

int fputc(int value, FILE *stream) {
  unsigned char byte = value;
  return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

char *fgets(char *buffer, int capacity, FILE *stream) {
  if (buffer == NULL || capacity <= 0 || stream == NULL) {
    return NULL;
  }
  int length = 0;
  while (length + 1 < capacity) {
    int value = fgetc(stream);
    if (value == EOF) {
      break;
    }
    buffer[length++] = value;
    if (value == '\n') {
      break;
    }
  }
  if (length == 0) {
    return NULL;
  }
  buffer[length] = '\0';
  return buffer;
}

int fputs(const char *text, FILE *stream) {
  size_t length = strlen(text);
  return fwrite(text, 1, length, stream) == length ? 0 : EOF;
}

int fprintf(FILE *stream, const char *format, ...) {
  char *buffer = malloc(1024);
  if (buffer == NULL) {
    return EOF;
  }
  va_list arguments;
  va_start(arguments, format);
  int length = vsprintf(buffer, format, arguments);
  va_end(arguments);
  int result = fwrite(buffer, 1, length, stream) == (size_t)length ? length
                                                                  : EOF;
  free(buffer);
  return result;
}

int feof(FILE *stream) { return stream != NULL && stream->eof ? EOF : 0; }

int ferror(FILE *stream) { return stream != NULL && stream->error ? EOF : 0; }

int getc(FILE *stream) { return fgetc(stream); }
