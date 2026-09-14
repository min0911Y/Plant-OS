// SPDX-License-Identifier: Zlib
#include "internal.h"
#include <dlfcn.h>
#include <futex.h>
#include <ipc.h>
#include <limits.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

// LWJGL loads libglfw with a local handle, so its DT_NEEDED dependencies are
// not visible through the process-wide lookup scope.  Reopen the exact system
// module and expose its symbols to GLFW's per-module lookups.
void *_glfwPlatformLoadModule(const char *path) {
  if (strcmp(path, "libEGL.so") && strcmp(path, "libGL.so"))
    return NULL;
  const char prefix[] = "/lib/";
  size_t prefix_length = sizeof(prefix) - 1;
  size_t path_length = strlen(path);
  if (path_length >= PATH_MAX - prefix_length)
    return NULL;
  char module_path[PATH_MAX];
  memcpy(module_path, prefix, prefix_length);
  memcpy(module_path + prefix_length, path, path_length + 1);
  return dlopen(module_path, RTLD_NOW | RTLD_GLOBAL);
}

void _glfwPlatformFreeModule(void *module) { dlclose(module); }

GLFWproc _glfwPlatformGetModuleSymbol(void *module, const char *name) {
  return (GLFWproc)dlsym(module, name);
}

void _glfwPlatformInitTimer(void) {}
uint64_t _glfwPlatformGetTimerValue(void) { return monotonic_ns(); }
uint64_t _glfwPlatformGetTimerFrequency(void) { return 1000000000; }

void _glfwPostEmptyEventPlantOS(void) {
  __atomic_store_n(&_glfw.plantos.pending, 1, __ATOMIC_RELEASE);
  os_futex_wake(&_glfw.plantos.pending, 1);
}

static void *eventThread(void *argument) {
  _glfw.plantos.eventTarget = (rpc_endpoint_t){NowTaskID(), ipc_generation()};
  __atomic_store_n(&_glfw.plantos.ready, 1, __ATOMIC_RELEASE);
  os_futex_wake(&_glfw.plantos.ready, 1);
  while (!__atomic_load_n(&_glfw.plantos.stop, __ATOMIC_ACQUIRE)) {
    char buffer[IPC_MAX_MSG_SIZE];
    ipc_msg_t message;
    int result = ipc_recv_any(buffer, sizeof(buffer), &message, 0);
    if (result < 0) {
      __atomic_store_n(&_glfw.plantos.stop, 1, __ATOMIC_RELEASE);
      _glfwPostEmptyEventPlantOS();
      break;
    }
    // This thread owns a private IPC inbox and never runs application
    // callbacks.
    if (message.type == RPC_TYPE_NOTIFY && message.size >= sizeof(rpc_wire_t)) {
      rpc_wire_t wire;
      memcpy(&wire, buffer, sizeof(wire));
      if (wire.opcode == GUI_RPC_EVENT_READY)
        _glfwPostEmptyEventPlantOS();
    }
  }
  return NULL;
}

GLFWbool _glfwInitEventsPlantOS(void) {
  if (pthread_create(&_glfw.plantos.eventThread, NULL, eventThread, NULL)) {
    _glfwInputError(GLFW_PLATFORM_ERROR,
                    "Plant OS: Cannot create event receiver");
    return GLFW_FALSE;
  }
  while (!__atomic_load_n(&_glfw.plantos.ready, __ATOMIC_ACQUIRE))
    os_futex_wait(&_glfw.plantos.ready, 0, UINT64_MAX);
  return GLFW_TRUE;
}

void _glfwTerminateEventsPlantOS(void) {
  if (!_glfw.plantos.eventThread)
    return;
  __atomic_store_n(&_glfw.plantos.stop, 1, __ATOMIC_RELEASE);
  ipc_msg_t wake = {
      .peer_tid = _glfw.plantos.eventTarget.tid,
      .peer_generation = _glfw.plantos.eventTarget.generation,
      .flags = IPC_NOWAIT,
  };
  ipc_send_msg(&wake);
  pthread_join(_glfw.plantos.eventThread, NULL);
  _glfw.plantos.eventThread = NULL;
}

static void waitEvents(uint64_t deadline) {
  while (!__atomic_load_n(&_glfw.plantos.pending, __ATOMIC_ACQUIRE)) {
    if (__atomic_load_n(&_glfw.plantos.stop, __ATOMIC_ACQUIRE)) {
      _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: Event receiver stopped");
      return;
    }
    int result = os_futex_wait(&_glfw.plantos.pending, 0, deadline);
    if (result == FUTEX_TIMED_OUT)
      break;
    if (result != FUTEX_OK && result != FUTEX_CHANGED &&
        result != FUTEX_INTERRUPTED) {
      _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: Event wait failed (%d)",
                      result);
      return;
    }
  }
  _glfwPollEventsPlantOS();
}

void _glfwWaitEventsPlantOS(void) { waitEvents(UINT64_MAX); }

void _glfwWaitEventsTimeoutPlantOS(double timeout) {
  uint64_t now = monotonic_ns();
  double duration = timeout * 1000000000.0;
  uint64_t deadline = duration >= (double)(UINT64_MAX - now)
                          ? UINT64_MAX
                          : now + (uint64_t)duration;
  waitEvents(deadline);
}
