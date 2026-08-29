#include <arch/x86/io.h>
// bmp.c ：位图解析
#include <dos.h>
#include <fs.h>
#include <io.h>
static unsigned char *bmp_load(const char *path, uint32_t *size) {
  vfs_stat_t status;
  if (vfs_stat(current_task()->fs_context, path, &status) < 0 ||
      status.size < 54) {
    return NULL;
  }
  FILE *stream = fopen(path, "rb");
  unsigned char *buffer = malloc(status.size);
  if (stream == NULL || buffer == NULL ||
      fread(buffer, 1, status.size, stream) != status.size) {
    if (stream != NULL) {
      fclose(stream);
    }
    free(buffer);
    return NULL;
  }
  fclose(stream);
  *size = status.size;
  return buffer;
}
bool BMPVIEW8(char *path, unsigned char *vram, int xsize) {
  uint32_t size;
  unsigned char *p = bmp_load(path, &size);
  if (p == NULL) {
    return false;
  }
  int i, j;
  unsigned short pxsize, pysize, start;
  unsigned int length;
  if (p[0] != 'B' || p[1] != 'M') {
    free(p);
    return false;
  }
  pxsize = *(unsigned short *)(p + 0x12);
  pysize = *(unsigned short *)(p + 0x16);
  length = *(unsigned int *)(p + 2);
  start = *(unsigned short *)(p + 0xa);
  if (length > size || start >= size) {
    free(p);
    return false;
  }
  x86_port_write8(VGA_DAC_WRITE_INDEX, 0);
  for (i = 0; i != 256; i++) {
    x86_port_write8(VGA_DAC_DATA, p[0x36 + i * 4 + 2] / 4);
    x86_port_write8(VGA_DAC_DATA, p[0x36 + i * 4 + 1] / 4);
    x86_port_write8(VGA_DAC_DATA, p[0x36 + i * 4] / 4);
  }
  for (i = 0; i < pysize; i++) {
    for (j = 0; j < pxsize; j++) {
      // Draw_Px(j, i, p[length - (i * pxsize + pxsize - j)]);
      vram[i * xsize + j] = p[length - (i * pxsize + pxsize - j)];
    }
  }
  free(p);
  return true;
}
bool BMPVIEW32(char *path, unsigned char *vram, int xsize) {
  uint32_t size;
  unsigned char *buf = bmp_load(path, &size);
  if (buf == NULL) {
    return false;
  }
  if (buf[0] != 'B' || buf[1] != 'M') {
    free(buf);
    return false;
  }
  int i, j;
  int offset;
  int width, height;
  unsigned char r, g, b;
  int x, y;
  width = *(int *)(buf + 18);
  height = *(int *)(buf + 22);
  offset = *(int *)(buf + 10);
  if (offset < 0 || width <= 0 || height <= 0 ||
      (uint64_t)offset + (uint64_t)width * height * 3 > size) {
    free(buf);
    return false;
  }
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      b = buf[offset + i * width * 3 + j * 3 + 0];
      g = buf[offset + i * width * 3 + j * 3 + 1];
      r = buf[offset + i * width * 3 + j * 3 + 2];
      x = j;
      y = height - 1 - i;
      Draw_Px_32(vram, x, y, r, g, b, xsize);
    }
  }
  free(buf);
  return true;
}
