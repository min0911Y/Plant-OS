#include <framebuffer.h>
#include <gui.h>
#include <gui_rpc.h>
#include <ipc.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>
#include <time.h>

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

static int gui_test_mouse(void) {
  window_t window = create_window("GUI mouse test", 64, 64, 256, 192);
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
    int event = window_get_event(window);
    if (event == -1) {
      sleep(10);
      continue;
    }
    if (event != GUI_EVENT_MOUSE_STAY && event != GUI_EVENT_MOUSE_CLICK_LEFT &&
        event != GUI_EVENT_MOUSE_CLICK_RIGHT &&
        event != GUI_EVENT_MOUSE_WHEEL) {
      break;
    }
    int position = window_get_event(window);
    int x = (position >> 16) & 0xffff;
    int y = position & 0xffff;
    int wheel = event == GUI_EVENT_MOUSE_WHEEL ? window_get_event(window) : 0;
    if (event == GUI_EVENT_MOUSE_STAY && position >= 0 &&
        (x != 64 || y != 64)) {
      continue;
    }
    if (position < 0 || x != 64 || y != 64) {
      logkf("GUIMOUSE FAIL event=%d position=%d,%d\n", event, x, y);
      break;
    }
    if (event == GUI_EVENT_MOUSE_STAY) {
      observed |= 1;
    } else if (event == GUI_EVENT_MOUSE_CLICK_LEFT) {
      observed |= 2;
    } else if (event == GUI_EVENT_MOUSE_CLICK_RIGHT) {
      observed |= 4;
    } else if (wheel == 1) {
      observed |= 8;
    }
  }
  close_window(window);
  logkf("GUIMOUSE %s events=%u\n", observed == 15 ? "PASS" : "FAIL", observed);
  return observed == 15 ? 0 : 41;
}

static int gui_test_basic(void) {
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
  return 0;
}

static int gui_stress_load_process(unsigned parent_tid, unsigned index,
                                   unsigned window_processes) {
  window_t window = NULL;
  if (index < window_processes) {
    window = create_window("GUI stress load", (int)(index * 13 % 320),
                           (int)(index * 17 % 240), 648, 428);
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
                                  64 + (int)index * 280, 64, 258, 181);
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

int main(int argc, char **argv) {
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
  result = gui_test_basic();
  if (result != 0) {
    return result;
  }
  if (argc == 2 && strcmp(argv[1], "mouse") == 0) {
    result = gui_test_mouse();
    if (result != 0) {
      return result;
    }
  }

  logkf("GUITEST PASS gui_pid=%d\n", gui_pid);
  return 0;
}
