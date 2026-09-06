#include <errno.h>
#include <platform.h>
#include <pleditor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syntax.h>
#include <syscall.h>

int main(int argc, char **argv) {
  if (argc > 2) {
    fprintf(stderr, "Usage: editor [file]\n");
    return 1;
  }
  if (!pleditor_platform_init()) {
    fprintf(stderr, "editor: terminal unavailable\n");
    return 1;
  }
  if (atexit(pleditor_platform_cleanup) != 0) {
    pleditor_platform_cleanup();
    return 1;
  }

  pleditor_state state;
  pleditor_init(&state);
  pleditor_syntax_init(&state);
  if (argc == 2 && !pleditor_open(&state, argv[1])) {
    int error = errno;
    pleditor_free(&state);
    pleditor_platform_cleanup();
    fprintf(stderr, "editor: %s: %s\n", argv[1], strerror(error));
    return 1;
  }
  pleditor_set_status_message(
      &state,
      "Ctrl-S save | Ctrl-Q quit | Ctrl-F find | Ctrl-Z undo | Ctrl-Y redo");
  logkf("EDITOR ready columns=%d rows=%d\n", state.screen_cols,
        state.screen_rows);
  while (!state.should_quit) {
    pleditor_refresh_screen(&state);
    int key = pleditor_platform_read_key();
    if (key == PLEDITOR_KEY_ERR) {
      pleditor_free(&state);
      return 1;
    }
    pleditor_handle_keypress(&state, key);
  }
  pleditor_free(&state);
  return 0;
}
