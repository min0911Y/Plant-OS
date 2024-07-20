#include <dos.h>
static void fartty_putchar(struct tty *res, int c) {
  u32 args[10];
  args[0] = 0;
  args[1] = (u32)res->vram;
  args[2] = c;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);

  int nx, ny, nc;
  nx         = args[7];
  ny         = args[8];
  nc         = args[9];
  res->x     = nx;
  res->y     = ny;
  res->color = nc;
}
static void fartty_MoveCursor(struct tty *res, int x, int y) {
  u32 args[10];
  args[0] = 1;
  args[1] = (u32)res->vram;
  args[2] = x;
  args[3] = y;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  res->x  = x;
  res->y  = y;
  if (res->cur_moving == 0) { return; }
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);
}
static void fartty_clear(struct tty *res) {
  u32 args[10];
  args[0] = 2;
  args[1] = (u32)res->vram;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);
  int nx, ny, nc;
  nx         = args[7];
  ny         = args[8];
  nc         = args[9];
  res->x     = nx;
  res->y     = ny;
  res->color = nc;
}
static void fartty_screen_ne(struct tty *res) {
  u32 args[10];
  args[0] = 3;
  args[1] = (u32)res->vram;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);
  int nx, ny, nc;
  nx         = args[7];
  ny         = args[8];
  nc         = args[9];
  res->x     = nx;
  res->y     = ny;
  res->color = nc;
}
static void fartty_Draw_Box(struct tty *res, int x, int y, int x1, int y1, u8 color) {
  u32 args[10];
  args[0] = 4;
  args[1] = (u32)res->vram;
  args[2] = x;
  args[3] = y;
  args[4] = x1;
  args[5] = y1;
  args[6] = (u32)color;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);
  int nx, ny, nc;
  nx         = args[7];
  ny         = args[8];
  nc         = args[9];
  res->x     = nx;
  res->y     = ny;
  res->color = nc;
}
static int fartty_fifo_status(struct tty *res) {
  u32 args[10];
  args[0] = 5;
  args[1] = (u32)res->vram;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);
  int nx, ny, nc;
  nx         = args[7];
  ny         = args[8];
  nc         = args[9];
  res->x     = nx;
  res->y     = ny;
  res->color = nc;
  return args[0];
}
static int fartty_fifo_get(struct tty *res) {
  u32 args[10];
  args[0] = 6;
  args[1] = (u32)res->vram;
  args[7] = res->x;
  args[8] = res->y;
  args[9] = res->color;
  call_across_page((u32(*)(void *))res->reserved[0], res->reserved[1], args);
  int nx, ny, nc;
  nx         = args[7];
  ny         = args[8];
  nc         = args[9];
  res->x     = nx;
  res->y     = ny;
  res->color = nc;
  return args[0];
}

struct tty *fartty_alloc(void *vram, unsigned handle, unsigned cr3, int xsize, int ysize) {
  struct tty *ftty;
  ftty = tty_alloc(vram, xsize, ysize, fartty_putchar, fartty_MoveCursor, fartty_clear,
                   fartty_screen_ne, fartty_Draw_Box, fartty_fifo_status, fartty_fifo_get);
  tty_set_reserved(ftty, handle, cr3, 0, 0);
  return ftty;
}
