#define NOREAD
#include <dos.h>
#include <fcntl.h>
typedef struct OPEN {
  u8      *buf;
  u32      p;
  u32      size;
  u8      *path;
  unsigned arrsz;
  int      wg;
} OPEN;
int close(int fd) {
  logk("Close(%08x)\n", fd);
  OPEN *fp = (OPEN *)fd;
  logk("size=%d\n", fp->size);
  if (fp->wg) { EDIT_FILE(fp->path, fp->buf, fp->size, 0); }
  free(fp->buf);
  free(fp->path);
  free(fp);
  return 0;
}

int open(const char *pathname, int flags, u32 mode) {
  printk("OPEN:%s\n", pathname);
  OPEN *fp;
  fp = malloc(sizeof(OPEN));
  FILE *fp1;
  fp1       = fopen(pathname, "wb");
  fp->size  = fp1->fileSize;
  fp->buf   = malloc(fp->size);
  fp->path  = malloc(strlen(fp1->name) + 1);
  fp->arrsz = fp->size;
  strcpy(fp->path, pathname);
  memcpy(fp->buf, fp1->buffer, fp->size);
  fclose(fp1);
  fp->p  = 0;
  fp->wg = 0;
  return (int)fp;
}

u32 read(int fd, void *buf, u32 count) {
  OPEN *fp = (OPEN *)fd;
  int   i;
  for (i = 0; i < count; i++) {
    if (fp->p >= fp->size) { break; }
    ((u8 *)buf)[i] = fp->buf[fp->p++];
  }
  return i;
}
void rc(int fd, u8 c) {
  OPEN *fp = (OPEN *)fd;
  if (fp->size < fp->arrsz) {
    fp->buf[fp->p++] = c;
  } else {
    fp->arrsz += 4096;
    u8 *re     = malloc(fp->arrsz);
    memcpy(re, fp->buf, fp->size);
    free(fp->buf);
    fp->buf          = re;
    fp->buf[fp->p++] = c;
  }
  fp->size++;
  fp->wg = 1;
}
u32 write(int fd, const void *buf, u32 nbyte) {
  OPEN *fp = (OPEN *)fd;
  logk("fp=%08x\n", fp);
  for (int i = 0; i < nbyte; i++) {
    rc(fd, ((u8 *)buf)[i]);
  }
  return nbyte;
}
u32 lseek(int fd, u32 offset, int whence) {
  OPEN *fp = (OPEN *)fd;
  if (whence == 0) {
    fp->p = offset;
  } else if (whence == 1) {
    fp->p += offset;
  } else if (whence == 2) {
    fp->p = fp->size + offset;
  } else {
    return -1;
  }
  return 0;
}