#include "runtime_lifecycle.h"
#include "stdio_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

FILE *stdout;
FILE *stdin;
FILE *stderr;

enum stdio_mode {
  STDIO_READ = 1u << 0,
  STDIO_WRITE = 1u << 1,
  STDIO_APPEND = 1u << 2,
  STDIO_OWN_BUFFER = 1u << 3,
  STDIO_LINE_BUFFERED = 1u << 4,
};

enum stdio_direction {
  STDIO_DIRECTION_NONE,
  STDIO_DIRECTION_READ,
  STDIO_DIRECTION_WRITE,
};

struct stdio_memory {
  char **output;
  size_t *output_length;
  unsigned char *data;
  size_t position, length, capacity;
};

struct FILE {
  struct FILE *previous;
  struct FILE *next;
  unsigned char *buffer;
  size_t buffer_capacity;
  char *temporary_path;
  size_t buffer_length;
  size_t buffer_position;
  int descriptor;
  struct stdio_memory *memory;
  _Atomic unsigned int mode;
  pthread_mutex_t lock;
  enum stdio_direction direction;
  int unget_character;
  unsigned char eof;
  unsigned char error;
  unsigned char registered;
};

static FILE *stdio_streams;
static pthread_mutex_t registry_lock = {.type = PTHREAD_MUTEX_RECURSIVE};
static FILE *stdio_fdopen_impl(int descriptor, const char *mode);

static int stdio_parse_mode(const char *mode, unsigned int *stdio_mode,
                            int *open_flags) {
  if (mode == NULL || *mode == '\0') {
    return -1;
  }
  unsigned int stream_mode;
  int flags;
  if (*mode == 'r') {
    stream_mode = STDIO_READ;
    flags = O_RDONLY;
  } else if (*mode == 'w') {
    stream_mode = STDIO_WRITE;
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  } else if (*mode == 'a') {
    stream_mode = STDIO_WRITE | STDIO_APPEND;
    flags = O_WRONLY | O_CREAT | O_APPEND;
  } else {
    return -1;
  }
  bool plus = false;
  for (mode++; *mode != '\0'; mode++) {
    if (*mode == 'b') {
      continue;
    }
    if (*mode == '+' && !plus) {
      plus = true;
      stream_mode |= STDIO_READ | STDIO_WRITE;
      flags = (flags & ~O_ACCMODE) | O_RDWR;
      continue;
    }
    return -1;
  }
  *stdio_mode = stream_mode;
  *open_flags = flags;
  return 0;
}

static void stdio_register(FILE *stream) {
  pthread_mutex_lock(&registry_lock);
  stream->registered = 1;
  stream->next = stdio_streams;
  if (stdio_streams != NULL) {
    stdio_streams->previous = stream;
  }
  stdio_streams = stream;
  pthread_mutex_unlock(&registry_lock);
}

static void stdio_unregister(FILE *stream) {
  if (!stream->registered) {
    return;
  }
  pthread_mutex_lock(&registry_lock);
  if (stream->previous != NULL) {
    stream->previous->next = stream->next;
  } else {
    stdio_streams = stream->next;
  }
  if (stream->next != NULL) {
    stream->next->previous = stream->previous;
  }
  stream->registered = 0;
  pthread_mutex_unlock(&registry_lock);
}

static FILE *stdio_stream_create(int descriptor, unsigned int mode,
                                 struct stdio_memory *memory) {
  FILE *stream = malloc(sizeof(*stream));
  if (stream == NULL) {
    return NULL;
  }
  memset(stream, 0, sizeof(*stream));
  if (descriptor > 2) {
    stream->buffer = malloc(BUFSIZ);
    if (stream->buffer == NULL) {
      free(stream);
      return NULL;
    }
  }
  stream->descriptor = descriptor;
  stream->memory = memory;
  stream->buffer_capacity = stream->buffer ? BUFSIZ : 0;
  if (stream->buffer)
    mode |= STDIO_OWN_BUFFER;
  stream->mode = mode;
  stream->lock.type = PTHREAD_MUTEX_RECURSIVE;
  stream->unget_character = EOF;
  return stream;
}

static FILE *stdio_fdopen_impl(int descriptor, const char *mode) {
  unsigned int stream_mode;
  int ignored_flags;
  if (descriptor < 0 ||
      stdio_parse_mode(mode, &stream_mode, &ignored_flags) != 0) {
    errno = EINVAL;
    return NULL;
  }
  if (descriptor > 2) {
    struct stat status;
    if (fstat(descriptor, &status) != 0) {
      return NULL;
    }
  }
  FILE *stream = stdio_stream_create(descriptor, stream_mode, NULL);
  if (stream)
    stdio_register(stream);
  return stream;
}

FILE *open_memstream(char **buffer, size_t *size) {
  if (!buffer || !size) {
    errno = EINVAL;
    return NULL;
  }
  struct stdio_memory *memory = calloc(1, sizeof(*memory));
  if (!memory)
    return NULL;
  memory->data = calloc(1, 1);
  if (!memory->data) {
    free(memory);
    return NULL;
  }
  memory->capacity = 1;
  memory->output = buffer;
  memory->output_length = size;
  FILE *stream = stdio_stream_create(-1, STDIO_WRITE, memory);
  if (!stream) {
    free(memory->data);
    free(memory);
    return NULL;
  }
  *buffer = (char *)memory->data;
  *size = 0;
  stdio_register(stream);
  return stream;
}

static int stdio_close_backend(FILE *stream) {
  if (!stream->memory)
    return close(stream->descriptor);
  free(stream->memory);
  stream->memory = NULL;
  return 0;
}

static off_t stdio_seek_backend(FILE *stream, off_t offset, int whence) {
  struct stdio_memory *memory = stream->memory;
  if (!memory)
    return lseek(stream->descriptor, offset, whence);
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    errno = EINVAL;
    return -1;
  }
  off_t base = whence == SEEK_SET   ? 0
               : whence == SEEK_CUR ? memory->position
                                    : memory->length;
  if (offset < -base || offset > LONG_MAX - base) {
    errno = EOVERFLOW;
    return -1;
  }
  return memory->position = base + offset;
}

static int stdio_setvbuf(FILE *stream, char *buffer, int mode, size_t size) {
  if (!stream || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) ||
      (buffer && !size && mode != _IONBF)) {
    errno = EINVAL;
    return -1;
  }
  bool owned = false;
  if (mode == _IONBF) {
    buffer = NULL;
    size = 0;
  } else if (!buffer) {
    size = size ? size : BUFSIZ;
    buffer = malloc(size);
    if (!buffer)
      return -1;
    owned = true;
  }
  if (fflush(stream)) {
    if (owned)
      free(buffer);
    return -1;
  }
  if (stream->mode & STDIO_OWN_BUFFER)
    free(stream->buffer);
  stream->buffer = (unsigned char *)buffer;
  stream->buffer_capacity = size;
  stream->mode = (stream->mode & ~(STDIO_OWN_BUFFER | STDIO_LINE_BUFFERED)) |
                 (owned ? STDIO_OWN_BUFFER : 0) |
                 (mode == _IOLBF ? STDIO_LINE_BUFFERED : 0);
  return 0;
}
void setbuf(FILE *stream, char *buffer) {
  setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, BUFSIZ);
}

static FILE *stdio_freopen(const char *filename, const char *mode,
                           FILE *stream) {
  if (!stream || !filename) {
    errno = EINVAL;
    return NULL;
  }
  unsigned stream_mode;
  int flags;
  if (stdio_parse_mode(mode, &stream_mode, &flags)) {
    errno = EINVAL;
    return NULL;
  }
  fflush(stream);
  stdio_close_backend(stream);
  if (stream->temporary_path) {
    unlink(stream->temporary_path);
    free(stream->temporary_path);
    stream->temporary_path = NULL;
  }
  int descriptor = open(filename, flags, 0);
  if (descriptor < 0) {
    stdio_unregister(stream);
    if (stream->mode & STDIO_OWN_BUFFER)
      free(stream->buffer);

    return NULL;
  }
  stream->descriptor = descriptor;
  stream->mode =
      stream_mode | (stream->mode & (STDIO_OWN_BUFFER | STDIO_LINE_BUFFERED));
  stream->direction = STDIO_DIRECTION_NONE;
  stream->buffer_length = stream->buffer_position = 0;
  stream->unget_character = EOF;
  stream->eof = stream->error = 0;
  return stream;
}

FILE *tmpfile(void) {
  static uint32_t sequence;
  for (;;) {
    char *path = NULL;
    uint32_t token = __atomic_fetch_add(&sequence, 1, __ATOMIC_RELAXED) ^
                     (uint32_t)monotonic_ns() ^ (uint32_t)NowTaskID();
    if (asprintf(&path, "%c:/%08x.tmp", api_current_drive(), token) < 0)
      return NULL;
    int descriptor = open(path, O_RDWR | O_CREAT | O_EXCL, 0);
    if (descriptor < 0) {
      free(path);
      if (errno == EEXIST)
        continue;
      return NULL;
    }
    FILE *stream =
        stdio_stream_create(descriptor, STDIO_READ | STDIO_WRITE, NULL);
    if (!stream) {
      int error = errno;
      close(descriptor);
      unlink(path);
      free(path);
      errno = error;
      return NULL;
    }
    stream->temporary_path = path;
    stdio_register(stream);
    return stream;
  }
}

FILE *fopen(const char *filename, const char *mode) {
  unsigned int stream_mode;
  int flags;
  if (filename == NULL || stdio_parse_mode(mode, &stream_mode, &flags) != 0) {
    errno = EINVAL;
    return NULL;
  }
  int descriptor = open(filename, flags, 0);
  if (descriptor < 0) {
    return NULL;
  }
  FILE *stream = stdio_stream_create(descriptor, stream_mode, NULL);
  if (stream == NULL) {
    close(descriptor);
    errno = ENOMEM;
  } else {
    stdio_register(stream);
  }
  return stream;
}

static size_t stdio_write_all(FILE *stream, const unsigned char *buffer,
                              size_t length) {
  struct stdio_memory *memory = stream->memory;
  if (memory) {
    if (length > (size_t)LONG_MAX - memory->position) {
      errno = EOVERFLOW;
      stream->error = 1;
      return 0;
    }
    size_t end = memory->position + length;
    if (end >= memory->capacity) {
      size_t capacity =
          memory->capacity <= SIZE_MAX / 2 ? memory->capacity * 2 : end + 1;
      if (capacity <= end)
        capacity = end + 1;
      unsigned char *data = realloc(memory->data, capacity);
      if (!data) {
        stream->error = 1;
        return 0;
      }
      memory->data = data;
      memory->capacity = capacity;
    }
    if (memory->position > memory->length)
      memset(memory->data + memory->length, 0,
             memory->position - memory->length);
    memcpy(memory->data + memory->position, buffer, length);
    memory->position = end;
    if (memory->length < end)
      memory->length = end;
    memory->data[memory->length] = 0;
    return length;
  }
  size_t completed = 0;
  while (completed < length) {
    ssize_t written =
        write(stream->descriptor, buffer + completed, length - completed);
    if (written <= 0) {
      stream->error = 1;
      return completed;
    }
    completed += written;
  }
  return completed;
}

static int stdio_fflush(FILE *stream) {
  if (stream == NULL) {
    int status = 0;
    pthread_mutex_lock(&registry_lock);
    for (FILE *current = stdio_streams; current != NULL;
         current = current->next) {
      if ((current->mode & STDIO_WRITE) && fflush(current) != 0) {
        status = EOF;
      }
    }
    pthread_mutex_unlock(&registry_lock);
    return status;
  }
  if (stream->direction == STDIO_DIRECTION_WRITE &&
      stream->buffer_length != 0) {
    size_t written =
        stdio_write_all(stream, stream->buffer, stream->buffer_length);
    if (written != stream->buffer_length) {
      stream->buffer_length -= written;
      memmove(stream->buffer, stream->buffer + written, stream->buffer_length);
      return EOF;
    }
  } else if (stream->direction == STDIO_DIRECTION_READ) {
    size_t unread = stream->buffer_length - stream->buffer_position;
    if (stream->unget_character != EOF) {
      unread++;
    }
    if (unread != 0 && stream->descriptor > 2 &&
        lseek(stream->descriptor, -(off_t)unread, SEEK_CUR) < 0) {
      stream->error = 1;
      return EOF;
    }
  }
  if (stream->memory) {
    struct stdio_memory *memory = stream->memory;
    *memory->output = (char *)memory->data;
    *memory->output_length =
        memory->position < memory->length ? memory->position : memory->length;
  }
  stream->buffer_length = 0;
  stream->buffer_position = 0;
  stream->direction = STDIO_DIRECTION_NONE;
  stream->unget_character = EOF;
  return 0;
}

static int stdio_fclose(FILE *stream) {
  if (stream == NULL) {
    return EOF;
  }
  int status = fflush(stream);
  stdio_unregister(stream);
  if (stdio_close_backend(stream) != 0) {
    status = EOF;
  }
  if (stream->temporary_path) {
    if (unlink(stream->temporary_path))
      status = EOF;
    free(stream->temporary_path);
  }
  if (stream->mode & STDIO_OWN_BUFFER)
    free(stream->buffer);

  return status;
}

static int stdio_prepare(FILE *stream, enum stdio_direction direction) {
  if (stream->direction != STDIO_DIRECTION_NONE &&
      stream->direction != direction && fflush(stream) != 0) {
    return -1;
  }
  stream->direction = direction;
  return 0;
}

static size_t stdio_fread(void *buffer, size_t size, size_t count,
                          FILE *stream) {
  if (stream == NULL || (stream->mode & STDIO_READ) == 0 || size == 0 ||
      count == 0 || count > SIZE_MAX / size ||
      stdio_prepare(stream, STDIO_DIRECTION_READ) != 0) {
    return 0;
  }
  size_t requested = size * count;
  size_t completed = 0;
  unsigned char *output = buffer;
  if (stream->unget_character != EOF && requested != 0) {
    output[completed++] = stream->unget_character;
    stream->unget_character = EOF;
  }
  while (completed < requested) {
    if (stream->descriptor == 0) {
      int character = getch();
      if (character >= 0 && character <= 255)
        output[completed++] = character;
      continue;
    }
    if (!stream->buffer_capacity) {
      ssize_t received =
          read(stream->descriptor, output + completed, requested - completed);
      if (received <= 0) {
        stream->error |= received < 0;
        stream->eof |= received == 0;
        break;
      }
      completed += received;
      continue;
    }
    if (stream->buffer_position == stream->buffer_length) {
      ssize_t read_count =
          read(stream->descriptor, stream->buffer, stream->buffer_capacity);
      if (read_count < 0) {
        stream->error = 1;
        break;
      }
      if (read_count == 0) {
        stream->eof = 1;
        break;
      }
      stream->buffer_length = read_count;
      stream->buffer_position = 0;
    }
    size_t chunk = stream->buffer_length - stream->buffer_position;
    if (chunk > requested - completed) {
      chunk = requested - completed;
    }
    memcpy(output + completed, stream->buffer + stream->buffer_position, chunk);
    stream->buffer_position += chunk;
    completed += chunk;
  }
  return completed / size;
}

static size_t stdio_fwrite(const void *buffer, size_t size, size_t count,
                           FILE *stream) {
  if (stream == NULL || (stream->mode & STDIO_WRITE) == 0 || size == 0 ||
      count == 0 || count > SIZE_MAX / size ||
      stdio_prepare(stream, STDIO_DIRECTION_WRITE) != 0) {
    return 0;
  }
  size_t requested = size * count;
  if (!stream->buffer_capacity) {
    return stdio_write_all(stream, buffer, requested) / size;
  }
  size_t completed = 0;
  const unsigned char *input = buffer;
  while (completed < requested) {
    if (stream->buffer_length == stream->buffer_capacity &&
        fflush(stream) != 0) {
      break;
    }
    stream->direction = STDIO_DIRECTION_WRITE;
    size_t chunk = stream->buffer_capacity - stream->buffer_length;
    if (chunk > requested - completed) {
      chunk = requested - completed;
    }
    memcpy(stream->buffer + stream->buffer_length, input + completed, chunk);
    stream->buffer_length += chunk;
    completed += chunk;
    if ((stream->mode & STDIO_LINE_BUFFERED) &&
        memchr(input + completed - chunk, '\n', chunk) && fflush(stream) != 0)
      break;
  }
  return completed / size;
}

static int stdio_fseek(FILE *stream, long offset, int whence) {
  if (stream == NULL || fflush(stream) != 0 ||
      stdio_seek_backend(stream, offset, whence) < 0) {
    if (stream != NULL) {
      stream->error = 1;
    }
    return -1;
  }
  stream->eof = 0;
  return 0;
}

static long stdio_ftell(FILE *stream) {
  if (stream == NULL) {
    return -1;
  }
  off_t position = stdio_seek_backend(stream, 0, SEEK_CUR);
  if (position < 0) {
    stream->error = 1;
    return -1;
  }
  if (stream->direction == STDIO_DIRECTION_READ) {
    position -= stream->buffer_length - stream->buffer_position;
    if (stream->unget_character != EOF) {
      position--;
    }
  } else if (stream->direction == STDIO_DIRECTION_WRITE) {
    position += stream->buffer_length;
  }
  return position;
}

int fgetc(FILE *stream) {
  unsigned char value;
  return fread(&value, 1, 1, stream) == 1 ? value : EOF;
}

int fputc(int value, FILE *stream) {
  unsigned char byte = value;
  return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

static char *stdio_fgets(char *buffer, int capacity, FILE *stream) {
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

int putchar(int character) { return fputc(character, stdout); }

int puts(const char *text) {
  stdio_stream_lock(stdout);
  int result = fputs(text, stdout) == EOF ? EOF : fputc('\n', stdout);
  stdio_stream_unlock(stdout);
  return result;
}

int fprintf(FILE *stream, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vfprintf(stream, format, arguments);
  va_end(arguments);
  return result;
}

static int stdio_feof(FILE *stream) {
  return stream != NULL && stream->eof ? EOF : 0;
}

static int stdio_ferror(FILE *stream) {
  return stream != NULL && stream->error ? EOF : 0;
}

static void stdio_clearerr(FILE *stream) {
  if (stream) {
    stream->eof = 0;
    stream->error = 0;
  }
}

int getc(FILE *stream) { return fgetc(stream); }

static int stdio_ungetc(int character, FILE *stream) {
  if (stream == NULL || character == EOF || stream->unget_character != EOF) {
    return EOF;
  }
  stream->unget_character = (unsigned char)character;
  stream->eof = 0;
  return stream->unget_character;
}

void stdio_initialize(void) {
  if (stdin != NULL || stdout != NULL || stderr != NULL) {
    return;
  }
  stdin = stdio_fdopen_impl(0, "r");
  stdout = stdio_fdopen_impl(1, "w");
  stderr = stdio_fdopen_impl(2, "w");
}

void stdio_shutdown(void) {
  pthread_mutex_lock(&registry_lock);
  while (stdio_streams != NULL) {
    fclose(stdio_streams);
  }
  stdin = NULL;
  stdout = NULL;
  stderr = NULL;
  pthread_mutex_unlock(&registry_lock);
}

static int stdio_fileno(FILE *fp) {
  if (fp == NULL || fp->descriptor < 0) {
    errno = EBADF;
    return -1;
  }
  return fp->descriptor;
}
int vfprintf(FILE *fp, const char *fmt, va_list args) {
  char local[256];
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(local, sizeof(local), fmt, copy);
  va_end(copy);
  if (length < 0)
    return -1;
  char *buffer = local;
  if ((size_t)length >= sizeof(local)) {
    buffer = malloc((size_t)length + 1);
    if (!buffer)
      return -1;
    if (vsnprintf(buffer, (size_t)length + 1, fmt, args) != length) {
      free(buffer);
      errno = EINVAL;
      return -1;
    }
  }
  int result = fwrite(buffer, 1, length, fp) == (size_t)length ? length : -1;
  if (buffer != local)
    free(buffer);
  return result;
}
int printf(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int rv = vfprintf(stdout, format, ap);
  va_end(ap);

  return rv;
}
void rewind(FILE *stream) { fseek(stream, 0, SEEK_SET); }
FILE *fdopen(int fd, const char *mode) { return stdio_fdopen_impl(fd, mode); }
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

void stdio_stream_lock(FILE *stream) {
  if (stream)
    pthread_mutex_lock(&stream->lock);
}
void stdio_stream_unlock(FILE *stream) {
  if (stream)
    pthread_mutex_unlock(&stream->lock);
}

#define STDIO_LOCKED(type, name, parameters, arguments)                        \
  type name parameters {                                                       \
    stdio_stream_lock(stream);                                                 \
    type result = stdio_##name arguments;                                      \
    stdio_stream_unlock(stream);                                               \
    return result;                                                             \
  }
STDIO_LOCKED(size_t, fread,
             (void *buffer, size_t size, size_t count, FILE *stream),
             (buffer, size, count, stream))
STDIO_LOCKED(size_t, fwrite,
             (const void *buffer, size_t size, size_t count, FILE *stream),
             (buffer, size, count, stream))
STDIO_LOCKED(int, fflush, (FILE * stream), (stream))
STDIO_LOCKED(int, setvbuf, (FILE * stream, char *buffer, int mode, size_t size),
             (stream, buffer, mode, size))
STDIO_LOCKED(int, fseek, (FILE * stream, long offset, int whence),
             (stream, offset, whence))
STDIO_LOCKED(long, ftell, (FILE * stream), (stream))
STDIO_LOCKED(char *, fgets, (char *buffer, int size, FILE *stream),
             (buffer, size, stream))
STDIO_LOCKED(int, ungetc, (int character, FILE *stream), (character, stream))
STDIO_LOCKED(int, feof, (FILE * stream), (stream))
STDIO_LOCKED(int, ferror, (FILE * stream), (stream))
STDIO_LOCKED(int, fileno, (FILE * stream), (stream))
#undef STDIO_LOCKED

void clearerr(FILE *stream) {
  stdio_stream_lock(stream);
  stdio_clearerr(stream);
  stdio_stream_unlock(stream);
}

int fclose(FILE *stream) {
  if (!stream)
    return EOF;
  pthread_mutex_lock(&registry_lock);
  stdio_stream_lock(stream);
  int result = stdio_fclose(stream);
  stdio_stream_unlock(stream);
  free(stream);
  pthread_mutex_unlock(&registry_lock);
  return result;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
  if (!stream) {
    errno = EINVAL;
    return NULL;
  }
  pthread_mutex_lock(&registry_lock);
  stdio_stream_lock(stream);
  FILE *result = stdio_freopen(path, mode, stream);
  stdio_stream_unlock(stream);
  if (!stream->registered)
    free(stream);
  pthread_mutex_unlock(&registry_lock);
  return result;
}

void runtime_stdio_fork_lock(void) {
  pthread_mutex_lock(&registry_lock);
  for (FILE *stream = stdio_streams; stream; stream = stream->next)
    stdio_stream_lock(stream);
}

void runtime_stdio_fork_unlock(bool child) {
  for (FILE *stream = stdio_streams; stream; stream = stream->next) {
    if (child) {
      stream->lock.owner = NowTaskID();
      stream->lock.state = 1;
    }
    stdio_stream_unlock(stream);
  }
  if (child) {
    registry_lock.owner = NowTaskID();
    registry_lock.state = 1;
  }
  pthread_mutex_unlock(&registry_lock);
}
