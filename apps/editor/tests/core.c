/* Host-only harness for the portable core; see doc/editor.md. */
#include "platform.h"
#include "pleditor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t output_size;
static char document[40 * 81 + 1];
bool pleditor_platform_init(void) { return true; }
void pleditor_platform_cleanup(void) {}
bool pleditor_platform_get_size(int *rows, int *cols) {
  *rows = 25;
  *cols = 80;
  return true;
}
int pleditor_platform_read_key(void) { return PLEDITOR_KEY_ESC; }
void pleditor_platform_write(const char *text, size_t length) {
  assert(length && text[0] == '\033');
  output_size = length;
}
void *pleditor_platform_reallocate(void *old, size_t size) {
  void *result = realloc(old, size ? size : 1);
  assert(result);
  return result;
}
_Noreturn void pleditor_platform_error(const char *message) {
  fprintf(stderr, "%s\n", message);
  abort();
}
bool pleditor_platform_read_file(const char *name, char **buffer, size_t *len) {
  (void)name;
  *len = strlen(document);
  *buffer = strdup(document);
  return *buffer != NULL;
}
bool pleditor_platform_write_file(const char *name, const char *text,
                                  size_t length) {
  (void)name;
  assert(length == strlen(document));
  assert(!memcmp(text, document, length));
  return true;
}
int main(void) {
  for (unsigned row = 0; row < 40; row++) {
    for (unsigned col = 0; col < 80; col++)
      document[row * 81 + col] = (col & 1) ? '+' : '1';
    document[row * 81 + 80] = '\n';
  }
  pleditor_state state;
  pleditor_init(&state);
  assert(pleditor_open(&state, "dense.c"));
  pleditor_refresh_screen(&state);
  assert(output_size > (size_t)state.screen_rows * state.screen_cols * 5);
  state.screen_cols = 1;
  state.cx = 79;
  state.cy = 20;
  pleditor_refresh_screen(&state);
  assert(pleditor_get_line_number_width(&state) == 0);
  state.cx = 17;
  pleditor_insert_newline(&state);
  pleditor_apply_undo(&state);
  pleditor_apply_redo(&state);
  pleditor_apply_undo(&state);
  pleditor_save(&state);
  pleditor_free(&state);
  pleditor_init(&state);
  pleditor_handle_keypress(&state, PLEDITOR_PAGE_DOWN);
  assert(state.cy == 0);
  pleditor_insert_newline(&state);
  pleditor_apply_undo(&state);
  assert(state.cy == 0 && state.num_rows == 0);
  pleditor_apply_redo(&state);
  assert(state.num_rows == 1);
  pleditor_apply_undo(&state);
  assert(state.num_rows == 0);
  state.search_query = strdup("missing");
  pleditor_search_previous(&state);
  pleditor_refresh_screen(&state);
  pleditor_free(&state);
  puts("EDITOR CORE PASS: dense highlighting, narrow view, save, empty "
       "navigation/search");
}
