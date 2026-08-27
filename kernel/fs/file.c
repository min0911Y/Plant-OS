#include <dos.h>
#include <fs.h>
#include <limits.h>
#define READ 0x2
#define WRITE 0x4
#define APPEND 0x8
#define BIN 0x0
#define PLUS 0x10
int fseek(FILE *fp, int offset, int whence) {
  if (whence == 0) {
    fp->p = offset;
  } else if (whence == 1) {
    fp->p += offset;
  } else if (whence == 2) {
    fp->p = fp->fileSize + offset;
  } else {
    return -1;
  }
  return 0;
}
long ftell(FILE *stream) { return stream->p; }
#define CANREAD(flag) ((flag)&READ || (flag)&PLUS)
#define CANWRITE(flag) ((flag)&WRITE || (flag)&PLUS || (flag)&APPEND)
FILE *fopen(char *filename, char *mode) {
  if (filename == NULL || mode == NULL) {
    return NULL;
  }
  unsigned int flag = 0;
  while (*mode != '\0') {
    switch (*mode) {
    case 'a':
      flag |= APPEND;
      break;
    case 'b':
      break;
    case 'r':
      flag |= READ;
      break;
    case 'w':
      flag |= WRITE;
      break;
    case '+':
      flag |= PLUS;
      break;
    default:
      break;
    }
    mode++;
  }
  uint32_t file_size = vfs_filesize(filename);
  if (file_size == (uint32_t)-1 || file_size > INT_MAX) {
    return NULL;
  }
  FILE *fp = (FILE *)malloc(sizeof(FILE));
  if (fp == NULL) {
    return NULL;
  }
  memset(fp, 0, sizeof(FILE));
  if (flag & WRITE) {
    fp->fileSize = 0;
  } else {
    fp->fileSize = file_size;
  }
  uint32_t buffer_size = 0;
  if (flag & READ || flag & PLUS || flag & APPEND) {
    buffer_size = file_size;
  }
  if (flag & WRITE || flag & PLUS || flag & APPEND) {
    if (buffer_size > INT_MAX - 100) {
      free(fp);
      return NULL;
    }
    buffer_size += 100;
  }
  if (buffer_size == 0) {
    buffer_size = 1;
  }
  fp->buffer = malloc(buffer_size);
  if (fp->buffer == NULL) {
    free(fp);
    return NULL;
  }
  if (file_size != 0 && (flag & PLUS || flag & APPEND || flag & READ) &&
      !vfs_readfile(filename, (char *)fp->buffer)) {
    free(fp->buffer);
    free(fp);
    return NULL;
  }
  size_t filename_length = strlen(filename);
  if (filename_length >= INT_MAX) {
    free(fp->buffer);
    free(fp);
    return NULL;
  }
  fp->name = malloc(filename_length + 1);
  if (fp->name == NULL) {
    free(fp->buffer);
    free(fp);
    return NULL;
  }
  memcpy(fp->name, filename, filename_length + 1);
  fp->bufferSize = buffer_size;
  fp->p = 0;
  if (flag & APPEND) {
    fp->p = fp->fileSize;
  }
  fp->mode = flag;
  return fp;
}
int fgetc(FILE *stream) {
  if (CANREAD(stream->mode)) {
    if (stream->p >= stream->fileSize) {
      return EOF;
    } else {
      return stream->buffer[stream->p++];
    }
  } else {
    return EOF;
  }
}
int fputc(int ch, FILE *stream) {
  if (CANWRITE(stream->mode)) {
    //		printk("Current Buffer=%s\n",stream->buffer);
    if (stream->p >= stream->bufferSize) {
      if (stream->bufferSize > INT_MAX - 100) {
        return EOF;
      }
      unsigned char *replacement =
          realloc(stream->buffer, stream->bufferSize + 100);
      if (replacement == NULL) {
        return EOF;
      }
      stream->buffer = replacement;
      stream->bufferSize += 100;
    }
    if (stream->p >= stream->fileSize) {
      stream->fileSize++;
    }
    //		printk("Current Buffer=%s(A)\n",stream->buffer);
    stream->buffer[stream->p++] = ch;
    //	printk("Current Buffer=%s(B)\n",stream->buffer);
    return ch;
  }
  return EOF;
}
unsigned int fwrite(const void *ptr, unsigned int size, unsigned int nmemb,
                    FILE *stream) {
  if (CANWRITE(stream->mode)) {
    unsigned char *c_ptr = (unsigned char *)ptr;
    for (unsigned int i = 0; i < size * nmemb; i++) {
      fputc(c_ptr[i], stream);
    }
    return nmemb;
  } else {
    return 0;
  }
}
unsigned int fread(void *buffer, unsigned int size, unsigned int count,
                   FILE *stream) {
  if (CANREAD(stream->mode)) {
    unsigned char *c_ptr = (unsigned char *)buffer;
    for (unsigned int i = 0; i < size * count; i++) {
      int ch = fgetc(stream);
      if (ch == EOF) {
        return i;
      } else {
        c_ptr[i] = ch;
      }
    }
    return count;
  } else {
    return 0;
  }
}
int fclose(FILE *fp) {
  if (fp == NULL) {
    return EOF;
  }
  int status = 0;
  if (CANWRITE(fp->mode)) {
    //		printk("Save file.....(%s) Size =
    //%d\n",fp->buffer,fp->fileSize);
    //  Edit_File(fp->name, fp->buffer, fp->fileSize, 0);
    if (!vfs_writefile(fp->name, (char *)fp->buffer, fp->fileSize)) {
      status = EOF;
    }
  }
  free(fp->buffer);
  free(fp->name);
  free(fp);
  return status;
}
char *fgets(char *str, int n, FILE *stream) {
  if (CANREAD(stream->mode)) {
    for (int i = 0; i < n; i++) {
      int ch = fgetc(stream);
      if (ch == EOF) {
        if (i == 0) {
          return NULL;
        } else {
          break;
        }
      }
      if (ch == '\n') {
        break;
      }
      str[i] = ch;
    }
    return str;
  }
  return NULL;
}
int fputs(const char *str, FILE *stream) {
  if (CANWRITE(stream->mode)) {
    for (size_t i = 0; i < strlen(str); i++) {
      fputc(str[i], stream);
    }
    return 0;
  }
  return EOF;
}
int fprintf(FILE *stream, const char *format, ...) {
  if (CANWRITE(stream->mode)) {
    int len;
    va_list ap;
    va_start(ap, format);
    char *buf = malloc(1024);
    len = vsprintf(buf, format, ap);
    fputs(buf, stream);
    free(buf);
    va_end(ap);
    return len;
  } else {
    // printk("CAN NOT WRITE\n");
    return EOF;
  }
}

int feof(FILE *stream) {
  if (stream->p >= stream->fileSize) {
    return EOF;
  }
  return 0;
}
int ferror(FILE *stream) {
  (void)stream;
  return 0;
}
int getc(FILE *stream) { return fgetc(stream); }
int fsz(char *filename) { return vfs_filesize(filename); }

bool EDIT_FILE(char *name, char *dest, int length, int offset) {
  if (name == NULL || (length > 0 && dest == NULL) || length < 0 ||
      offset != 0) {
    return false;
  }
  if (vfs_filesize(name) == (uint32_t)-1) {
    if (!vfs_createfile(name)) {
      return false;
    }
  }
  return vfs_writefile(name, dest, length);
}
int Copy(char *path, char *path1) {
  if (path == NULL || path1 == NULL) {
    return -1;
  }
  int sz = fsz(path);
  if (sz < 0) {
    return -1;
  }
  char *path1_file_buffer = sz == 0 ? NULL : malloc(sz);
  if (sz != 0 && path1_file_buffer == NULL) {
    return -1;
  }
  if ((sz != 0 && !vfs_readfile(path, path1_file_buffer)) ||
      (vfs_filesize(path1) == (uint32_t)-1 && !vfs_createfile(path1)) ||
      !vfs_writefile(path1, path1_file_buffer, sz)) {
    free(path1_file_buffer);
    return -1;
  }
  free(path1_file_buffer);
  return 0;
}
