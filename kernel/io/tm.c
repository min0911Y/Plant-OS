#include <dos.h>
#include <platform.h>

typedef uint32_t text_pair_t __attribute__((may_alias));

static void text_fill(uint16_t *cells, int count, uint16_t value) {
  if (((uintptr_t)cells & (sizeof(text_pair_t) - 1)) == 0) {
    text_pair_t pair = value | (uint32_t)value << 16;
    text_pair_t *wide = (text_pair_t *)cells;
    for (int index = 0; index < count / 2; index++) {
      wide[index] = pair;
    }
    if (count & 1) {
      cells[count - 1] = value;
    }
    return;
  }
  for (int index = 0; index < count; index++) {
    cells[index] = value;
  }
}

void MoveCursor_TextMode(struct tty *res, int x, int y) {
  res->x = x;
  res->y = y;
  if (!res->cur_moving)
    return;
  uint16_t position = (uint16_t)(y * res->xsize + x);
  if (platform_is_text_vram(res->vram)) {
    platform_text_cursor_set(position);
  }
}

void putchar_TextMode(struct tty *res, int c) {
  if (res->x == res->xsize) {
    res->gotoxy(res, 0, res->y + 1);
  }
  if (c == '\n') {
    if (res->y == res->ysize - 1) {
      res->screen_ne(res);
      return;
    }
    res->MoveCursor(res, 0, res->y + 1);
    return;
  } else if (c == '\0') {
    return;
  } else if (c == '\b') {
    if (res->x == 0) {
      res->MoveCursor(res, res->xsize - 1, res->y - 1);
      *(unsigned char *)(res->vram + res->y * res->xsize * 2 + res->x * 2) = ' ';
      *(unsigned char *)(res->vram + res->y * res->xsize * 2 + res->x * 2 - 2 + 1) =
          res->color;
      return;
    }
    *(unsigned char *)(res->vram + res->y * res->xsize * 2 + res->x * 2 - 2) = ' ';
    *(unsigned char *)(res->vram + res->y * res->xsize * 2 + res->x * 2 - 2 + 1) =
        res->color;
    res->MoveCursor(res, res->x - 1, res->y);
    return;
  } else if (c == '\t') {
    // 制表符
    res->print(res, "    ");
    return;
  } else if (c == '\r') {
    res->MoveCursor(res, 0, res->y);
    return;
  }
  *(unsigned char *)(res->vram + res->y * res->xsize * 2 + res->x * 2) = c;
  *(unsigned char *)(res->vram + res->y * res->xsize * 2 + res->x * 2 + 1) = res->color;
  res->MoveCursor(res, res->x + 1, res->y);
}

void screen_ne_TextMode(struct tty *res) {
  if (res->xsize <= 0 || res->ysize <= 0) {
    return;
  }
  uint16_t *cells = res->vram;
  int visible_cells = res->xsize * (res->ysize - 1);
  uint16_t *source = cells + res->xsize;
  if ((((uintptr_t)cells | (uintptr_t)source) &
       (sizeof(text_pair_t) - 1)) == 0) {
    text_pair_t *destination_wide = (text_pair_t *)cells;
    const text_pair_t *source_wide = (const text_pair_t *)source;
    for (int index = 0; index < visible_cells / 2; index++) {
      destination_wide[index] = source_wide[index];
    }
    if (visible_cells & 1) {
      cells[visible_cells - 1] = source[visible_cells - 1];
    }
  } else {
    for (int index = 0; index < visible_cells; index++) {
      cells[index] = source[index];
    }
  }
  uint16_t blank = (uint16_t)res->color << 8 | ' ';
  text_fill(cells + visible_cells, res->xsize, blank);
  res->gotoxy(res, 0, res->ysize - 1);
  res->Raw_y++;
}

void clear_TextMode(struct tty *res) {
  if (res->xsize <= 0 || res->ysize <= 0) {
    return;
  }
  uint16_t *cells = res->vram;
  uint16_t blank = (uint16_t)res->color << 8 | ' ';
  text_fill(cells, res->xsize * res->ysize, blank);
  res->gotoxy(res, 0, 0);
  res->Raw_y = 0;
}
void Draw_Box_TextMode(struct tty *res, int x, int y, int x1, int y1,
                       unsigned char color) {
  for (int i = y; i < y1; i++) {
    for (int j = x; j < x1; j++) {
      *(unsigned char *)(res->vram + (i * res->xsize + j) * 2 + 1) = color;
    }
  }
}
bool now_tty_TextMode(struct tty *res) {
  return platform_is_text_vram(res->vram);
}
