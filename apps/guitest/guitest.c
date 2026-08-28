#include <gui.h>
#include <rpc.h>
#include <stdio.h>
#include <syscall.h>

static char gui_program[] = "gui.bin";

int main(void) {
  int gui_pid = fork();
  if (gui_pid < 0) {
    logkf("GUITEST FAIL fork\n");
    return 1;
  }
  if (gui_pid == 0) {
    return exec(gui_program, gui_program);
  }

  rpc_endpoint_t gui_service;
  int result = rpc_connect(GUI_SERVICE_NAME, &gui_service, 30000);
  if (result != RPC_OK) {
    logkf("GUITEST FAIL connect=%d\n", result);
    return 2;
  }
  window_t window = create_window("GUI RPC test", 48, 48, 64, 64);
  if (window == NULL) {
    logkf("GUITEST FAIL create\n");
    return 3;
  }
  unsigned *framebuffer = window_get_fb(window);
  if (framebuffer == NULL) {
    logkf("GUITEST FAIL framebuffer\n");
    close_window(window);
    return 4;
  }

  draw_px(window, 3, 5, 0x00ff00ff);
  if (framebuffer[5 * 64 + 3] != 0x00ff00ff) {
    logkf("GUITEST FAIL shared framebuffer\n");
    close_window(window);
    return 5;
  }
  for (int i = 0; i < 16; i++) {
    draw_px(window, i, 0, 0x0000ffff);
    window_refresh(window, i << 16, ((i + 1) << 16) | 1);
  }
  window_refresh(window, 0, (64 << 16) | 64);
  window_start_recv_keyboard(window);
  (void)window_get_event(window);
  if (window_get_key_press_status(window) != 0 ||
      window_get_key_up_status(window) != 0) {
    logkf("GUITEST FAIL shared queues\n");
    window_stop_recv_keyboard(window);
    close_window(window);
    return 6;
  }
  window_stop_recv_keyboard(window);
  close_window(window);
  logkf("GUITEST PASS gui_pid=%d\n", gui_pid);
  return 0;
}
