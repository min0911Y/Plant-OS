// bmp.c ：位图解析
#include <fs.h>
bool BMPVIEW8(char *path, u8 *vram, int xsize) {
  if (fsz(path) == -1) { return false; }
  int   i, j;
  FILE *fp = fopen(path, "r");
  char *p  = fp->buffer;
  u16   pxsize, pysize, start;
  u32   length;
  if (p[0] != 'B' || p[1] != 'M') { return false; }
  pxsize = *(u16 *)(p + 0x12);
  pysize = *(u16 *)(p + 0x16);
  length = *(u32 *)(p + 2);
  start  = *(u16 *)(p + 0xa);
  io_out8(VGA_DAC_WRITE_INDEX, 0);
  for (i = 0; i != 256; i++) {
    io_out8(VGA_DAC_DATA, p[0x36 + i * 4 + 2] / 4);
    io_out8(VGA_DAC_DATA, p[0x36 + i * 4 + 1] / 4);
    io_out8(VGA_DAC_DATA, p[0x36 + i * 4] / 4);
  }
  for (i = 0; i < pysize; i++) {
    for (j = 0; j < pxsize; j++) {
      // Draw_Px(j, i, p[length - (i * pxsize + pxsize - j)]);
      vram[i * xsize + j] = p[length - (i * pxsize + pxsize - j)];
    }
  }
  fclose(fp);
  return true;
}
bool BMPVIEW32(char *path, u8 *vram, int xsize) {
  if (fsz(path) == -1) { return false; }
  FILE *fp  = fopen(path, "r");
  u8   *buf = fp->buffer;
  if (buf[0] != 'B' || buf[1] != 'M') { return false; }
  int i, j;
  int offset;
  int width, height;
  u8  r, g, b;
  int x, y;
  width  = *(int *)(buf + 18);
  height = *(int *)(buf + 22);
  offset = *(int *)(buf + 10);
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
  fclose(fp);
  return true;
}