#include <framebuffer.h>
#include <gui.h>
#include <gui_rpc.h>
#include <ipc.h>
#include <rpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>
#include <time.h>
#include <vm.h>

enum {
  GUI_STRESS_WINDOW_PROCESSES = 48,
  GUI_STRESS_IDLE_PROCESSES = 208,
  GUI_STRESS_PROCESSES =
      GUI_STRESS_WINDOW_PROCESSES + GUI_STRESS_IDLE_PROCESSES,
  GUI_STRESS_FRAMES = 200,
  GUI_STRESS_FRAME_MS = 50,
  GUI_STRESS_STALL_MS = 2000,
  GUI_STRESS_IPC_TIMEOUT_MS = 60000,
  GUI_STRESS_READY = 0x475201,
  GUI_STRESS_START = 0x475202,
  GUI_STRESS_RELEASE = 0x475203,
  GUI_STRESS_DONE = 0x475204,
};

typedef struct {
  unsigned index;
  int status;
} gui_stress_status_t;

static char gui_program[] = "gui.bin";
static char guitest_program[] = "guitest.bin";

typedef struct {
  unsigned parent_tid;
  uintptr_t cookie;
} gui_thread_probe_t;

static void gui_thread_probe(gui_thread_probe_t *probe) {
  uintptr_t reply[] = {(uintptr_t)probe, probe->cookie};
  ipc_send_to(probe->parent_tid, 0, 0, reply, sizeof(reply), 5000);
  _exit(0);
}

static int gui_test_thread(void) {
  void *stack = malloc(32 * 1024);
  if (stack == NULL) {
    return 1;
  }
  gui_thread_probe_t probe = {
      .parent_tid = NowTaskID(),
      .cookie = ~(uintptr_t)0x12345678,
  };
  uintptr_t entry = (uintptr_t)gui_thread_probe;
  uintptr_t top = (uintptr_t)stack + 32 * 1024;
  int valid =
      AddThread("probe", entry, 0, 0) < 0 && AddThread("probe", 0, top, 0) < 0;
  int tid = AddThread("probe", entry, top, (uintptr_t)&probe);
  uintptr_t reply[2] = {0};
  ipc_msg_t message;
  valid = tid > 0 &&
          ipc_recv_from(tid, reply, sizeof(reply), &message, 5000) ==
              sizeof(reply) &&
          reply[0] == (uintptr_t)&probe && reply[1] == probe.cookie && valid;
  if (tid > 0) {
    SubThread(tid);
  }
  free(stack);
  logkf("GUITEST THREAD %s\n", valid ? "PASS" : "FAIL");
  return !valid;
}

static int gui_test_memory_pressure(void) {
  if (gui_test_thread())
    return 1;
  struct mapping {
    void *address;
    size_t length;
  };
  size_t capacity = mem_total() / (1024 * 1024) + 512;
  struct mapping *mappings = calloc(capacity, sizeof(*mappings));
  void *stack = malloc(32 * 1024);
  volatile unsigned char *cow = malloc(2 * 1024 * 1024);
  if (!mappings || !stack || !cow) {
    free(mappings);
    free(stack);
    free((void *)cow);
    return 1;
  }
  unsigned parent_tid = NowTaskID();
  int children[2];
  for (unsigned i = 0; i < 2; i++) {
    int child = fork();
    if (child == 0) {
      ipc_send_to(parent_tid, GUI_STRESS_READY, 0, NULL, 0, 5000);
      ipc_msg_t message;
      // The second case faults inside the kernel while copying an IPC payload.
      void *buffer = i ? (void *)(cow + VM_PAGE_SIZE) : NULL;
      unsigned size = i ? sizeof(unsigned) : 0;
      if (ipc_recv_from(parent_tid, buffer, size, &message, 30000) < 0)
        return 1;
      if (!i)
        cow[VM_PAGE_SIZE] = 42;
      return 0;
    }
    ipc_msg_t ready;
    if (child < 0 || ipc_recv_from(child, NULL, 0, &ready, 5000) < 0)
      return 1;
    children[i] = child;
  }
  // Make bookkeeping private before exhausting pages shared with the child.
  memset(mappings, 0, capacity * sizeof(*mappings));
  (void)vm_map(NULL, 1); // Materialize errno before the eventual ENOMEM result.
  size_t count = 0;
  // Leave room for the kernel stack, but insufficient memory to finish a new
  // thread's input queues. AddThread must fail before publishing that thread.
  for (size_t length = 1024 * 1024; length >= VM_PAGE_SIZE; length /= 256) {
    while (count < capacity && mem_total() / VM_PAGE_SIZE - mem_used() >
                                   19 + length / VM_PAGE_SIZE) {
      void *address = vm_map(NULL, length);
      if (!address)
        break;
      mappings[count++] = (struct mapping){address, length};
    }
  }
  size_t before = mem_used();
  int failed = 0;
  gui_thread_probe_t probe = {.parent_tid = NowTaskID(), .cookie = 0};
  for (unsigned i = 0; i < 64; i++) {
    int tid = AddThread("oom-probe", (uintptr_t)gui_thread_probe,
                        (uintptr_t)stack + 32 * 1024, (uintptr_t)&probe);
    if (tid >= 0) {
      SubThread(tid);
      failed = 1;
    }
    if (mem_used() != before)
      failed = 1;
  }
  for (unsigned i = 0; i < 2; i++) {
    while (count < capacity) {
      void *address = vm_map(NULL, VM_PAGE_SIZE);
      if (!address)
        break;
      mappings[count++] = (struct mapping){address, VM_PAGE_SIZE};
    }
    unsigned payload = 42;
    failed |=
        ipc_send_to(children[i], GUI_STRESS_RELEASE, 0, i ? &payload : NULL,
                    i ? sizeof(payload) : 0, 5000) != IPC_OK;
    failed |= waittid(children[i]) != -1;
  }
  while (count) {
    struct mapping *mapping = &mappings[--count];
    failed |= vm_unmap(mapping->address, mapping->length) != 0;
  }
  free(stack);
  free(mappings);
  free((void *)cow);
  failed |= gui_test_thread() != 0;
  failed |= exec("timetest.bin", "timetest.bin") != 0;
  logkf("MEMORYPRESSURE %s failures=64 rollback, user/kernel COW OOM, thread "
        "and exec recovery\n",
        failed ? "FAIL" : "PASS");
  return failed;
}

typedef struct {
  int status;
  bool finished;
  unsigned char stack[32 * 1024];
} gui_terminal_launch_t;

static void gui_terminal_load_worker(gui_terminal_launch_t *launch) {
  launch->status = exec("term.bin", "term.bin");
  __atomic_store_n(&launch->finished, true, __ATOMIC_RELEASE);
  _exit(launch->status);
}

static unsigned gui_terminal_load_report(unsigned attempted, unsigned requested,
                                         unsigned *shell_count) {
  task_info_t *tasks = NULL;
  size_t count = 0;
  unsigned terminals = 0, shells = 0;
  if (task_list(&tasks, &count) == 0) {
    for (size_t i = 0; i < count; i++) {
      if (tasks[i].tid != tasks[i].tgid || tasks[i].state == TASK_INFO_ZOMBIE)
        continue;
      terminals += strcmp(tasks[i].name, "term.bin") == 0;
      shells += strcmp(tasks[i].name, "psh.bin") == 0;
    }
  }
  free(tasks);
  logkf("TERMLOAD attempted=%u/%u terminals=%u shells=%u tasks=%u "
        "used_pages=%llu\n",
        attempted, requested, terminals, shells, (unsigned)count,
        (unsigned long long)mem_used());
  *shell_count = shells;
  return terminals;
}

static int gui_test_terminal_load(unsigned count) {
  if (sizeof(gui_terminal_launch_t *) > SIZE_MAX / count)
    return 2;
  gui_terminal_launch_t **launches = calloc(count, sizeof(*launches));
  if (!launches)
    return 2;
  unsigned failed = 0;
  for (unsigned i = 0; i < count; i++) {
    gui_terminal_launch_t *launch = calloc(1, sizeof(*launch));
    int tid =
        launch ? AddThread("term-load", (uintptr_t)gui_terminal_load_worker,
                           (uintptr_t)(launch->stack + sizeof(launch->stack)),
                           (uintptr_t)launch)
               : -1;
    if (tid < 0) {
      free(launch);
      failed++;
      logkf("TERMLOAD launch failed index=%u\n", i + 1);
    } else {
      launches[i] = launch;
    }
    // Keep every terminal open. Each launch follows the desktop's exec path.
    sleep(100);
    if ((i + 1) % 25 == 0 || i + 1 == count) {
      unsigned shells;
      gui_terminal_load_report(i + 1, count, &shells);
    }
  }
  sleep(5000);
  for (unsigned i = 0; i < count; i++) {
    gui_terminal_launch_t *launch = launches[i];
    if (launch && __atomic_load_n(&launch->finished, __ATOMIC_ACQUIRE)) {
      logkf("TERMLOAD exited index=%u status=%d\n", i + 1, launch->status);
      failed++;
    }
  }
  unsigned shells;
  unsigned terminals = gui_terminal_load_report(count, count, &shells);
  int status = exec("timetest.bin", "timetest.bin");
  logkf("TERMLOAD %s requested=%u terminals=%u shells=%u failed=%u probe=%d\n",
        !failed && terminals >= count && shells >= count && status == 0
            ? "PASS"
            : "FAIL",
        count, terminals, shells, failed, status);
  // Preserve windows and launcher stacks for the host's input/pixel checks.
  for (;;)
    sleep(1000);
}

static int gui_test_mouse(void) {
  window_t window = create_window("GUI mouse test", 64, 64, 256, 192, 0);
  framebuffer_info_t display;
  if (window == NULL || framebuffer_info(&display) < 0) {
    close_window(window);
    return 40;
  }

  logkf("GUIMOUSE READY origin=%u,%u target=128,128\n", display.width / 2,
        display.height / 2);
  unsigned observed = 0;
  unsigned deadline = (unsigned)clock() + 15000;
  while ((int)((unsigned)clock() - deadline) < 0 && observed != 15) {
    gui_event_t event;
    if (window_get_event(window, &event) <= 0) {
      sleep(10);
      continue;
    }
    if (event.type != GUI_EVENT_POINTER)
      break;
    if (event.x != 64 || event.y != 64)
      continue;
    observed |= 1;
    if (event.buttons & 1)
      observed |= 2;
    if (event.buttons & 2)
      observed |= 4;
    if (event.wheel > 0)
      observed |= 8;
  }
  close_window(window);
  logkf("GUIMOUSE %s events=%u\n", observed == 15 ? "PASS" : "FAIL", observed);
  return observed == 15 ? 0 : 41;
}

static int gui_test_interaction(void) {
  window_t window = create_window("Capture and resize", 64, 64, 256, 192,
                                  GUI_CREATE_RESIZABLE);
  window_t other = NULL;
  int result = 1;
#define INTERACTION_CHECK(condition)                                           \
  do {                                                                         \
    if (!(condition)) {                                                        \
      logkf("GUIINTERACT FAIL line=%u\n", __LINE__);                           \
      goto done;                                                               \
    }                                                                          \
  } while (0)
  INTERACTION_CHECK(window);
  gui_window_state_t initial_pointer, warped_pointer;
  INTERACTION_CHECK(window_get_state(window, &initial_pointer) == 0);
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_WARP_POINTER, 20, 40) ==
                    0);
  INTERACTION_CHECK(window_get_state(window, &warped_pointer) == 0 &&
                    warped_pointer.cursor_x == 20 &&
                    warped_pointer.cursor_y == 40);
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_WARP_POINTER,
                                   initial_pointer.cursor_x,
                                   initial_pointer.cursor_y) == 0);
  window_buffer_t buffer;
  INTERACTION_CHECK(window_get_buffer(window, &buffer) == 0);
  draw_px(window, 10, 30, 0x123456);
  INTERACTION_CHECK(window_present(window, (10 << 16) | 30, (11 << 16) | 31) ==
                    0);
  INTERACTION_CHECK(window_resize(window, 300, 220) == 0);
  INTERACTION_CHECK(window_get_buffer(window, &buffer) == 0 &&
                    buffer.width == 300 && buffer.height == 220 &&
                    buffer.pitch == 1200 &&
                    buffer.pixels[30 * 300 + 10] == 0x123456);
  void *pixels = buffer.pixels;
  INTERACTION_CHECK(window_resize(window, 0, 220) != 0 &&
                    window_resize(window, 32767, 32767) != 0 &&
                    window_get_fb(window) == pixels);
  for (unsigned y = 24; y < 216; y++)
    for (unsigned x = 4; x < 296; x++)
      buffer.pixels[y * 300 + x] = 0x2050a0;
  INTERACTION_CHECK(
      window_present_frame(window, (4 << 16) | 24, (296 << 16) | 216) == 0);
  gui_event_t event;
  while (window_get_event(window, &event) > 0) {
  }
  logkf("GUIINTERACT CAPTURE READY\n");
  uint64_t deadline = monotonic_ns() + 20000000000ull;
  unsigned observed = 0;
  while (monotonic_ns() < deadline && observed != 7) {
    if (window_get_event(window, &event) <= 0) {
      sleep(5);
      continue;
    }
    if (event.buttons == 3)
      observed |= 1;
    if (event.x >= 300 && event.buttons == 3)
      observed |= 2;
    if (event.x >= 300 && event.buttons == 0)
      observed |= 4;
  }
  INTERACTION_CHECK(observed == 7);
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_MOUSE_MODE,
                                   GUI_MOUSE_RELATIVE, 0) == 0);
  gui_window_state_t state;
  INTERACTION_CHECK(window_get_state(window, &state) == 0 &&
                    (state.flags & GUI_WINDOW_CAPTURED));
  logkf("GUIINTERACT RELATIVE READY\n");
  int dx = 0, dy = 0;
  deadline = monotonic_ns() + 20000000000ull;
  while (monotonic_ns() < deadline && (dx != 1400 || dy != -900)) {
    if (window_get_event(window, &event) <= 0) {
      sleep(5);
      continue;
    }
    if (event.relative) {
      dx += event.dx;
      dy += event.dy;
    }
  }
  INTERACTION_CHECK(dx == 1400 && dy == -900);
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_MOUSE_MODE, 0, 0) == 0);
  other = create_window("Focus", 450, 400, 64, 64, GUI_CREATE_HIDDEN);
  INTERACTION_CHECK(other);
  INTERACTION_CHECK(
      window_control(other, GUI_WINDOW_MOUSE_MODE, GUI_MOUSE_CAPTURE, 0) != 0);
  INTERACTION_CHECK(
      window_control(window, GUI_WINDOW_MOUSE_MODE, GUI_MOUSE_CAPTURE, 0) == 0);
  INTERACTION_CHECK(window_control(other, GUI_WINDOW_SHOW, 1, 0) == 0);
  INTERACTION_CHECK(window_get_state(window, &state) == 0 &&
                    !(state.flags & GUI_WINDOW_CAPTURED));
  close_window(other);
  other = NULL;
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_FOCUS, 0, 0) == 0);
  logkf("GUIINTERACT RESIZE READY\n");
  deadline = monotonic_ns() + 20000000000ull;
  do {
    INTERACTION_CHECK(window_get_state(window, &state) == 0);
    if (state.requested_width == 340 && state.requested_height == 250)
      break;
    sleep(5);
  } while (monotonic_ns() < deadline);
  INTERACTION_CHECK(state.requested_width == 340 &&
                    state.requested_height == 250);
  INTERACTION_CHECK(window_resize(window, 340, 250) == 0);
  INTERACTION_CHECK(window_get_buffer(window, &buffer) == 0 &&
                    buffer.pixels[40 * 340 + 20] == 0x2050a0);
  for (unsigned y = 24; y < 246; y++)
    for (unsigned x = 4; x < 336; x++)
      buffer.pixels[y * 340 + x] = 0x30a060;
  INTERACTION_CHECK(
      window_present_frame(window, (4 << 16) | 24, (336 << 16) | 246) == 0);
  window_start_recv_keyboard(window);
  logkf("GUIINTERACT PIXELS READY\n");
  deadline = monotonic_ns() + 15000000000ull;
  while (!window_get_key_press_status(window) && monotonic_ns() < deadline)
    sleep(5);
  INTERACTION_CHECK(window_get_key_press_data(window) == 0x39);
  INTERACTION_CHECK(window_resize(window, 180, 140) == 0);
  INTERACTION_CHECK(window_get_buffer(window, &buffer) == 0 &&
                    buffer.width == 180 && buffer.height == 140 &&
                    buffer.pixels[40 * 180 + 20] == 0x30a060);
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_MOUSE_MODE,
                                   GUI_MOUSE_RELATIVE, 0) == 0);
  INTERACTION_CHECK(window_control(window, GUI_WINDOW_HIDE, 0, 0) == 0);
  INTERACTION_CHECK(window_get_state(window, &state) == 0 &&
                    !(state.flags & GUI_WINDOW_CAPTURED));
  result = 0;
  logkf("GUIINTERACT PASS capture, relative, focus, resize, buffers\n");
done:
  close_window(other);
  close_window(window);
  return result;
#undef INTERACTION_CHECK
}

static int gui_test_usb_keyboard(void) {
  window_t window = create_window("USB keyboard test", 64, 64, 256, 192, 0);
  if (window == NULL)
    return 50;
  window_start_recv_keyboard(window);
  static const uint8_t presses[] = {0x1e, 0x2a, 0x30, 0xe0, 0x4b, 0x0e};
  static const uint8_t releases[] = {0x9e, 0xb0, 0xaa, 0xe0, 0xcb, 0x8e};
  unsigned p = 0, r = 0;
  logkf("USBKEY READY\n");
  uint64_t deadline = monotonic_ns() + 15000000000ull;
  while ((p < sizeof(presses) || r < sizeof(releases)) &&
         monotonic_ns() < deadline) {
    if (window_get_key_press_status(window)) {
      int code = window_get_key_press_data(window);
      if (p >= sizeof(presses) || code != presses[p++])
        return 51;
    }
    if (window_get_key_up_status(window)) {
      int code = window_get_key_up_data(window);
      if (r >= sizeof(releases) || code != releases[r++])
        return 52;
    }
    sleep(5);
  }
  if (p != sizeof(presses) || r != sizeof(releases))
    return 53;
  logkf("USBKEY REPEAT_READY\n");
  deadline = monotonic_ns() + 15000000000ull;
  unsigned repeated = 0;
  bool released = false;
  while (!released && monotonic_ns() < deadline) {
    while (window_get_key_press_status(window)) {
      if (window_get_key_press_data(window) != 0x2e)
        return 54;
      repeated++;
    }
    if (window_get_key_up_status(window)) {
      if (window_get_key_up_data(window) != 0xae)
        return 55;
      released = true;
    }
    sleep(5);
  }
  if (!released || repeated < 2)
    return 56;
  logkf("USBKEY HOLD_READY\n");
  deadline = monotonic_ns() + 15000000000ull;
  while (!window_get_key_press_status(window) && monotonic_ns() < deadline)
    sleep(5);
  if (window_get_key_press_data(window) != 0x2a)
    return 57;
  logkf("USBKEY REMOVE_READY\n");
  while (!window_get_key_up_status(window) && monotonic_ns() < deadline)
    sleep(5);
  if (window_get_key_up_data(window) != 0xaa)
    return 58;
  close_window(window);
  logkf("USBKEY PASS: keys, modifiers, arrows, repeat, unplug release\n");
  return 0;
}

static int gui_test_basic(void) {
  window_t window = create_window("GUI RPC test", 48, 48, 64, 64, 0);
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

  uint32_t icon[GUI_ICON_SIZE * GUI_ICON_SIZE] = {0};
  uint32_t background = framebuffer[4 * 64 + 4];
  icon[0] = 0xffff0000;
  icon[1] = 0xff00ff00;
  if (window_set_icon(window, icon) != 0 ||
      framebuffer[4 * 64 + 4] != 0xff0000 ||
      framebuffer[4 * 64 + 5] != 0x00ff00 ||
      window_set_title(window, "Icon test") != 0 ||
      framebuffer[4 * 64 + 4] != 0xff0000 ||
      window_set_icon(window, NULL) != 0 ||
      framebuffer[4 * 64 + 4] != background) {
    logkf("GUITEST FAIL title icon\n");
    close_window(window);
    return 5;
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
  gui_event_t event;
  (void)window_get_event(window, &event);
  if (window_get_key_press_status(window) != 0 ||
      window_get_key_up_status(window) != 0) {
    logkf("GUITEST FAIL shared queues\n");
    window_stop_recv_keyboard(window);
    close_window(window);
    return 6;
  }
  window_stop_recv_keyboard(window);
  close_window(window);
  return 0;
}

static int gui_test_frames(void) {
  enum { W = 648, H = 428, FIRST = (4 << 16) | 24,
         LAST = ((W - 4) << 16) | (H - 4) };
  window_t window = create_window("GUI frame ownership", 64, 64, W, H, 0);
  if (!window)
    return 1;
  window_start_recv_keyboard(window);
  uint32_t *planes[2] = {NULL, NULL};
  int status = 1;
  for (unsigned phase = 0; phase < 4; phase++) {
    if (phase == 1) {
      for (unsigned round = 0; round < 3; round++) {
        for (unsigned order = 0; order < 2; order++) {
          bool exchange = (order ^ (round & 1)) != 0;
          uint64_t present_ns = 0, start = monotonic_ns();
          for (unsigned frame = 0; frame < 240; frame++) {
            window_buffer_t buffer;
            if (window_get_buffer(window, &buffer))
              goto done;
            for (int y = 24; y < H - 4; y++)
              for (int x = 4; x < W - 4; x++)
                buffer.pixels[y * W + x] = (frame * 0x010307u) & 0xffffff;
            uint64_t before = monotonic_ns();
            int result = exchange ? window_present_frame(window, FIRST, LAST)
                                  : window_present(window, FIRST, LAST);
            present_ns += monotonic_ns() - before;
            if (result)
              goto done;
          }
          logkf("GUIFRAME BENCH mode=%s round=%u frames=240 elapsed_ns=%llu "
                "present_ns=%llu\n", exchange ? "exchange" : "copy", round,
                (unsigned long long)(monotonic_ns() - start),
                (unsigned long long)present_ns);
        }
      }
    }
    window_buffer_t buffer;
    if (window_get_buffer(window, &buffer) || buffer.pitch != W * 4 ||
        buffer.width != W || buffer.height != H)
      goto done;
    uint32_t color = (phase + 1) * 0x203040u;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        buffer.pixels[y * W + x] =
            x >= 4 && x < W - 4 && y >= 24 && y < H - 4 ? color : 0xff00ff;
    uint32_t *submitted = buffer.pixels;
    if (window_present_frame(window, FIRST, LAST) ||
        window_get_buffer(window, &buffer) || buffer.pixels == submitted)
      goto done;
    if (!phase) {
      planes[0] = submitted;
      planes[1] = buffer.pixels;
    } else if (submitted != planes[phase & 1] || buffer.pixels != planes[(phase & 1) ^ 1]) {
      goto done;
    }
    /* Poison only the newly acquired plane. Exposure must use the submitted
     * plane, even while a subsequent frame is being drawn. */
    for (int i = 0; i < W * H; i++)
      buffer.pixels[i] = 0xff00ff;
    if (phase == 2) {
      draw_px(window, 8, 28, 0xffffff);
      window_refresh(window, (8 << 16) | 28, (9 << 16) | 29);
      if (window_present(window, (8 << 16) | 28, (9 << 16) | 29))
        goto done;
    }
    window_t cover = create_window("Exposure", 100, 120, 128, 96, 0);
    if (!cover)
      goto done;
    close_window(cover);
    if (phase == 3) {
      unsigned parent = NowTaskID();
      int child = fork();
      if (child < 0)
        goto done;
      if (!child) {
        window_t orphan = create_window("Orphan frame", 100, 120, 128, 96, 0);
        window_buffer_t pixels;
        int result = !orphan || window_get_buffer(orphan, &pixels);
        if (!result) {
          for (unsigned i = 0; i < pixels.width * pixels.height; i++)
            pixels.pixels[i] = 0x123456;
          result = window_present_frame(orphan, 0, (128 << 16) | 96);
        }
        ipc_send_to(parent, 0, 0, &result, sizeof(result), 5000);
        _exit(result ? 1 : 0); // Intentionally leave a submitted window to reap.
      }
      int result = -1;
      ipc_msg_t message;
      if (ipc_recv_from(child, &result, sizeof(result), &message, 30000) !=
              sizeof(result) || result)
        goto done;
      sleep(2100); // GUI's owner-generation reaper runs every second.
    }
    logkf("GUIFRAME READY phase=%u x=64 y=64 width=%u height=%u color=%06x\n",
          phase, W, H, color);
    unsigned deadline = (unsigned)clock() + 30000;
    while (!window_get_key_press_status(window)) {
      if ((int)((unsigned)clock() - deadline) >= 0)
        goto done;
      sleep(10);
    }
    while (window_get_key_press_status(window))
      window_get_key_press_data(window);
  }
  status = 0;
done:
  close_window(window);
  logkf("GUIFRAME %s\n", status ? "FAIL" : "PASS");
  return status;
}

static int gui_stress_load_process(unsigned parent_tid, unsigned index,
                                   unsigned window_processes) {
  window_t window = NULL;
  if (index < window_processes) {
    window = create_window("GUI stress load", (int)(index * 13 % 320),
                           (int)(index * 17 % 240), 648, 428, 0);
  }

  gui_stress_status_t ready = {
      .index = index,
      .status = index >= window_processes || window != NULL ? 0 : 1,
  };
  if (ipc_send_to(parent_tid, GUI_STRESS_READY, index, &ready, sizeof(ready),
                  GUI_STRESS_IPC_TIMEOUT_MS) != IPC_OK) {
    if (window != NULL) {
      close_window(window);
    }
    return 2;
  }

  ipc_msg_t message;
  if (ipc_recv_from(parent_tid, NULL, 0, &message, 0) < 0 ||
      message.type != GUI_STRESS_RELEASE) {
    if (window != NULL) {
      close_window(window);
    }
    return 3;
  }
  if (window != NULL) {
    close_window(window);
  }

  gui_stress_status_t done = {.index = index, .status = 0};
  return ipc_send_to(parent_tid, GUI_STRESS_DONE, index, &done, sizeof(done),
                     GUI_STRESS_IPC_TIMEOUT_MS) == IPC_OK
             ? 0
             : 4;
}

static int gui_stress_refresh_process(unsigned parent_tid, unsigned index) {
  window_t window = create_window(index == 0 ? "GUI stress A" : "GUI stress B",
                                  64 + (int)index * 280, 64, 258, 181, 0);
  if (window == NULL) {
    return 10;
  }
  uint32_t *framebuffer = window_get_fb(window);
  if (framebuffer == NULL) {
    close_window(window);
    return 10;
  }
  gui_window_shared_t *shared =
      (gui_window_shared_t *)((unsigned char *)framebuffer - sizeof(*shared));
  gui_stress_status_t ready = {.index = index, .status = 0};
  if (ipc_send_to(parent_tid, GUI_STRESS_READY, index, &ready, sizeof(ready),
                  GUI_STRESS_IPC_TIMEOUT_MS) != IPC_OK) {
    close_window(window);
    return 11;
  }

  ipc_msg_t message;
  if (ipc_recv_from(parent_tid, NULL, 0, &message,
                    GUI_STRESS_IPC_TIMEOUT_MS) < 0 ||
      message.type != GUI_STRESS_START) {
    close_window(window);
    return 12;
  }

  const int last = (258 << 16) | 181;
  unsigned last_consumed = (unsigned)clock();
  for (unsigned frame = 0; frame < GUI_STRESS_FRAMES; frame++) {
    unsigned now = (unsigned)clock();
    uint32_t dirty = shared->damage.dirty;
    uint32_t queued = shared->damage.queued;
    if (!dirty && !queued) {
      last_consumed = now;
    } else if ((int)(now - last_consumed) >= GUI_STRESS_STALL_MS) {
      logkf("GUISTRESS worker=%u tid=%u frame=%u queued=%u dirty=%u\n",
            index, NowTaskID(), frame, queued, dirty);
      close_window(window);
      return 13;
    }
    framebuffer[(frame % 160 + 21) * 258 + (frame % 256 + 1)] =
        0x00010101u * (frame + 1);
    window_refresh(window, 0, last);
    sleep(GUI_STRESS_FRAME_MS);
  }

  unsigned deadline = (unsigned)clock() + GUI_STRESS_STALL_MS;
  while (shared->damage.dirty || shared->damage.queued) {
    if (shared->damage.dirty && !shared->damage.queued) {
      window_refresh(window, 0, last);
    }
    if ((int)((unsigned)clock() - deadline) >= 0) {
      close_window(window);
      return 14;
    }
    api_yield();
  }

  close_window(window);
  return 0;
}

static int gui_test_stress(unsigned window_processes) {
  unsigned parent_tid = NowTaskID();
  int children[GUI_STRESS_PROCESSES];
  bool load_ready[GUI_STRESS_PROCESSES] = {false};
  bool load_done[GUI_STRESS_PROCESSES] = {false};
  unsigned child_count = 0;
  int result = 0;

  for (unsigned i = 0; i < GUI_STRESS_PROCESSES; i++) {
    int pid = fork();
    if (pid < 0) {
      logkf("GUISTRESS FAIL fork load=%u\n", i);
      result = 20;
      break;
    }
    if (pid == 0) {
      return gui_stress_load_process(parent_tid, i, window_processes);
    }
    children[child_count++] = pid;
  }

  for (unsigned i = 0; i < child_count; i++) {
    gui_stress_status_t ready = {0};
    ipc_msg_t message = {0};
    int received =
        ipc_recv_any(&ready, sizeof(ready), &message,
                     GUI_STRESS_IPC_TIMEOUT_MS);
    if (received != sizeof(ready) || message.type != GUI_STRESS_READY ||
        ready.status != 0 || ready.index >= child_count ||
        load_ready[ready.index]) {
      logkf("GUISTRESS FAIL load ready=%d type=%u status=%d\n", received,
            message.type, ready.status);
      if (result == 0) {
        result = 21;
      }
      continue;
    }
    load_ready[ready.index] = true;
  }

  task_info_t *tasks = NULL;
  size_t task_count = 0;
  if (result == 0 && (task_list(&tasks, &task_count) != 0 ||
                      task_count < GUI_STRESS_PROCESSES)) {
    logkf("GUISTRESS FAIL task count=%u\n", (unsigned)task_count);
    result = 22;
  }
  free(tasks);

  int workers[2] = {-1, -1};
  unsigned worker_tids[2] = {0, 0};
  if (result == 0) {
    for (unsigned i = 0; i < 2; i++) {
      workers[i] = fork();
      if (workers[i] < 0) {
        result = 23;
        break;
      }
      if (workers[i] == 0) {
        char command[64];
        sprintf(command, "%s worker %u %u", guitest_program, parent_tid, i);
        return exec(guitest_program, command);
      }
    }
  }

  if (result == 0) {
    for (unsigned i = 0; i < 2; i++) {
      gui_stress_status_t ready = {0};
      ipc_msg_t message = {0};
      int received =
          ipc_recv_any(&ready, sizeof(ready), &message,
                       GUI_STRESS_IPC_TIMEOUT_MS);
      if (received != sizeof(ready) || message.type != GUI_STRESS_READY ||
          ready.status != 0 || ready.index > 1 ||
          worker_tids[ready.index] != 0) {
        result = 24;
        break;
      }
      worker_tids[ready.index] = message.peer_tid;
    }
  }
  if (result == 0) {
    for (unsigned i = 0; i < 2; i++) {
      if (ipc_send_to(worker_tids[i], GUI_STRESS_START, i, NULL, 0,
                      GUI_STRESS_IPC_TIMEOUT_MS) != IPC_OK) {
        result = 25;
        break;
      }
    }
  }
  for (unsigned i = 0; i < 2; i++) {
    if (workers[i] >= 0) {
      int status = waittid((unsigned)workers[i]);
      if (result == 0 && status != 0) {
        logkf("GUISTRESS FAIL worker=%u status=%d\n", i, status);
        result = 26;
      }
    }
  }

  for (unsigned i = 0; i < child_count; i++) {
    if (ipc_send_to((unsigned)children[i], GUI_STRESS_RELEASE, i, NULL, 0,
                    GUI_STRESS_IPC_TIMEOUT_MS) != IPC_OK &&
        result == 0) {
      result = 27;
    }
  }
  for (unsigned i = 0; i < child_count; i++) {
    gui_stress_status_t done = {0};
    ipc_msg_t message = {0};
    int received =
        ipc_recv_any(&done, sizeof(done), &message,
                     GUI_STRESS_IPC_TIMEOUT_MS);
    if ((received != sizeof(done) || message.type != GUI_STRESS_DONE ||
         done.status != 0 || done.index >= child_count ||
         load_done[done.index]) &&
        result == 0) {
      result = 28;
    } else if (received == sizeof(done) && message.type == GUI_STRESS_DONE &&
               done.status == 0 && done.index < child_count) {
      load_done[done.index] = true;
    }
  }

  if (result == 0) {
    logkf("GUISTRESS PASS tasks=%u windows=%u frames=%u\n",
          (unsigned)task_count, window_processes + 2,
          GUI_STRESS_FRAMES * 2);
  } else {
    logkf("GUISTRESS FAIL result=%d\n", result);
  }
  return result;
}

static int gui_test_terminal_client(void) {
  int failed = 0;
  clear();
  // These sequences are deliberately fragmented across individual syscalls.
  const char *output = "\033[38;2;12;34;56mABC\033[0m\033[3;4HZ";
  for (const char *p = output; *p; p++)
    putch(*p);
  failed |= get_xy() != (4 << 16 | 2);
  print("\033]0;split title");
  print("\033\\\033(BA");
  failed |= get_xy() != (5 << 16 | 2);
  print("\033[?1049h\033[Halt\033[?1049l");
  failed |= get_xy() != (5 << 16 | 2);
  goto_xy(2, 4);
  tty_stop_cur_moving();
  print("one\ntwo");
  tty_start_cur_moving();
  failed |= get_xy() != (3 << 16 | 5);
  Text_Draw_Box(10, 10, 15, 11, 0x17);
  failed |= get_xy() != (3 << 16 | 5);
  clear();
  failed |= get_xy() != 0;
  for (int i = 0; i < tty_get_xsize(); i++)
    putch('x');
  putch('y');
  failed |= get_xy() != (1 << 16 | 1);
  clear();
  for (int i = 0; i < tty_get_ysize() + 3; i++)
    print("scroll\n");
  failed |= get_xy() != tty_get_ysize() - 1;
  logkf("TERMTEST %s\n", failed ? "FAIL" : "PASS");
  return failed;
}

static int gui_editor_file_matches(const char *path, const char *expected) {
  char data[128];
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  size_t length = fread(data, 1, sizeof(data), file);
  int matches = !ferror(file) && length == strlen(expected) &&
                memcmp(data, expected, length) == 0;
  if (fclose(file) != 0)
    matches = 0;
  return matches;
}

static int gui_test_editor_client(void) {
  static const char initial[] = "int value = 1;\n";
  FILE *file = fopen("edtest.c", "wb");
  if (!file)
    return 1;
  bool written =
      fwrite(initial, 1, sizeof(initial) - 1, file) == sizeof(initial) - 1;
  if (fclose(file) != 0 || !written)
    return 1;
  remove("edsave.txt");
  remove("ednew.txt");
  // A directory must fail to open rather than appear as a new empty file.
  if (exec("editor.bin", "editor.bin /") == 0)
    return 1;
  clear();
  print("Editor session test\n");
  static const struct {
    char *command;
    const char *path, *contents;
  } cases[] = {
      {"editor.bin edtest.c", "edtest.c", "int value = 2;\n// saved\n"},
      {"editor.bin", "edsave.txt", "draft\n"},
      {"editor.bin edtest.c", "edtest.c", "int value = 2;\n// saved\n"},
      {"editor.bin ednew.txt", "ednew.txt", "new file\n"},
  };
  for (unsigned phase = 0; phase < sizeof(cases) / sizeof(cases[0]); phase++) {
    logkf("EDITORTEST START phase=%u\n", phase + 1);
    if (exec("editor.bin", cases[phase].command) != 0 ||
        !gui_editor_file_matches(cases[phase].path, cases[phase].contents)) {
      logkf("EDITORTEST FAIL phase=%u\n", phase + 1);
      return 1;
    }
    print("Editor returned. Press Enter to continue.\n");
    logkf("EDITORTEST RESTORED phase=%u\n", phase + 1);
    while (getch() != '\n') {
    }
  }
  remove("edtest.c");
  remove("edsave.txt");
  remove("ednew.txt");
  logkf("EDITORTEST PASS edit, undo, redo, search, save, reopen, discard\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "memory-pressure") == 0)
    return gui_test_memory_pressure();
  if (argc == 2 && strcmp(argv[1], "editor-client") == 0)
    return gui_test_editor_client();
  if (argc == 2 && strcmp(argv[1], "terminal-client") == 0)
    return gui_test_terminal_client();
  if (argc == 2 && strcmp(argv[1], "rendertm-client") == 0) {
    print("RenderTM return test\n");
    int saved_cursor = get_xy();
    int status = exec("rendertm.bin", "rendertm.bin --test");
    if (get_xy() != saved_cursor)
      status = 1;
    logkf("RENDERTM EXIT status=%d\n", status);
    return status;
  }
  if (argc == 4 && strcmp(argv[1], "worker") == 0) {
    int parent_tid = atoi(argv[2]);
    int index = atoi(argv[3]);
    if (parent_tid <= 0 || index < 0 || index > 1) {
      return 30;
    }
    return gui_stress_refresh_process((unsigned)parent_tid, (unsigned)index);
  }

  if (gui_test_thread() != 0) {
    return 1;
  }
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
  if (argc > 1 &&
      (strcmp(argv[1], "stress") == 0 || strcmp(argv[1], "capacity") == 0)) {
    return gui_test_stress(strcmp(argv[1], "stress") == 0
                               ? GUI_STRESS_WINDOW_PROCESSES
                               : 0);
  }
  if (argc == 2 && strcmp(argv[1], "frames") == 0)
    return gui_test_frames();
  result = gui_test_basic();
  if (result != 0) {
    return result;
  }
  if (argc == 3 && strcmp(argv[1], "terminal-load") == 0) {
    int count = atoi(argv[2]);
    return count > 0 ? gui_test_terminal_load((unsigned)count) : 2;
  }
  if (argc == 2 && strcmp(argv[1], "terminal") == 0) {
    result = exec("term.bin", "term.bin guitest.bin terminal-client");
    logkf("GUITERM %s\n", result == 0 ? "PASS" : "FAIL");
    return result;
  }
  if (argc == 2 && strcmp(argv[1], "rendertm") == 0)
    return exec("term.bin", "term.bin guitest.bin rendertm-client");
  if (argc == 2 && strcmp(argv[1], "renderhd") == 0) {
#ifdef PLANT_ARCH_X86_64
    return exec("renderhd.bin", "renderhd.bin --test");
#else
    return exec("renderhd.bin", "renderhd.bin --test --workers 2");
#endif
  }
  if (argc == 2 && strcmp(argv[1], "editor") == 0) {
    result = exec("term.bin", "term.bin guitest.bin editor-client");
    logkf("GUITEST EDITOR %s\n", result == 0 ? "PASS" : "FAIL");
    return result;
  }
  if (argc == 2 &&
      (strcmp(argv[1], "mouse") == 0 || strcmp(argv[1], "usb") == 0)) {
    result = gui_test_mouse();
    if (result != 0) {
      return result;
    }
  }

  if (argc == 2 && strcmp(argv[1], "mouse") == 0) {
    result = gui_test_interaction();
    if (result != 0)
      return result;
  }
  if (argc == 2 && strcmp(argv[1], "usb") == 0) {
    result = gui_test_usb_keyboard();
    if (result != 0)
      return result;
  }
  logkf("GUITEST PASS gui_pid=%d\n", gui_pid);
  return 0;
}
