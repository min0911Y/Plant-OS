#include <dos.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <platform.h>

static void terminal_free(void *address, size_t size) {
  (void)size;
  free(address);
}
static void terminal_sync(struct tty *tty) {
  size_t x, y;
  flanterm_get_cursor_pos(tty->vram, &x, &y);
  tty->x = x;
  tty->y = y;
  if (platform_video_console_visible()) {
    flanterm_flush(tty->vram);
    arch_dma_sync_for_device(NULL, 0);
  }
}
static void terminal_print(struct tty *tty, const char *text) {
  const char *start = text;
  for (; *text; text++) {
    if (*text != '\n')
      continue;
    flanterm_write(tty->vram, start, text - start);
    flanterm_write(tty->vram, "\r\n", 2);
    tty->Raw_y++;
    start = text + 1;
  }
  flanterm_write(tty->vram, start, text - start);
  terminal_sync(tty);
}
static void terminal_putchar(struct tty *tty, int character) {
  char ch = character;
  if (ch == '\n') {
    flanterm_write(tty->vram, "\r\n", 2);
    tty->Raw_y++;
  } else {
    flanterm_write(tty->vram, &ch, 1);
  }
  terminal_sync(tty);
}
static void terminal_move(struct tty *tty, int x, int y) {
  if (x < 0 || x >= tty->xsize || y < 0 || y >= tty->ysize)
    return;
  flanterm_set_cursor_pos(tty->vram, x, y);
  terminal_sync(tty);
}
static void terminal_clear(struct tty *tty) {
  flanterm_clear(tty->vram, true);
  terminal_sync(tty);
}
static void terminal_scroll(struct tty *tty) { terminal_print(tty, "\033[1S"); }
static void terminal_box(struct tty *tty, int x, int y, int x1, int y1,
                         uint8_t color) {
  size_t old_x, old_y;
  flanterm_get_cursor_pos(tty->vram, &old_x, &old_y);
  unsigned background = (color >> 4) & 7;
  unsigned ansi =
      (background & 2) | ((background & 1) << 2) | ((background & 4) >> 2);
  flanterm_set_text_bg(tty->vram, ansi, (color & 0x80) != 0);
  for (int row = y; row <= y1 && row < tty->ysize; row++) {
    if (row < 0)
      continue;
    for (int col = x; col <= x1 && col < tty->xsize; col++) {
      if (col < 0)
        continue;
      flanterm_set_cursor_pos(tty->vram, col, row);
      flanterm_write(tty->vram, " ", 1);
    }
  }
  flanterm_reset_text_bg(tty->vram);
  flanterm_set_cursor_pos(tty->vram, old_x, old_y);
  terminal_sync(tty);
}
struct tty *tty_console_create(void) {
  platform_video_info_t info;
  if (!platform_video_current_info(&info))
    return NULL;
  struct flanterm_context *terminal = flanterm_fb_init(
      malloc, terminal_free, (uint32_t *)info.framebuffer, info.width,
      info.height, info.pitch, info.red_size, info.red_shift, info.green_size,
      info.green_shift, info.blue_size, info.blue_shift, NULL, NULL, NULL, NULL,
      NULL, NULL, NULL, NULL, 0, 0, 1, 1, 1, 0, FLANTERM_FB_ROTATE_0, false);
  if (!terminal)
    return NULL;
  size_t columns, rows;
  flanterm_get_dimensions(terminal, &columns, &rows);
  struct tty *tty =
      tty_alloc(terminal, columns, rows, terminal_putchar, terminal_move,
                terminal_clear, terminal_scroll, terminal_box,
                default_tty_fifo_status, default_tty_fifo_get);
  if (!tty) {
    flanterm_deinit(terminal, terminal_free);
    return NULL;
  }
  tty->native_ansi = true;
  tty->print = terminal_print;
  tty->gotoxy = terminal_move;
  terminal_clear(tty);
  return tty;
}
