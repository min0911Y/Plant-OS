// SPDX-License-Identifier: MIT
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <futex.h>
#include <ipc.h>
#include <pthread.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      logkf("GLFWTEST FAIL line=%d %s\n", __LINE__, #condition);               \
      goto done;                                                               \
    }                                                                          \
  } while (0)

static struct {
  unsigned keys, releases, characters, motion, presses, buttons_up, scroll,
      close;
} input;

static int failAllocation = -1;
static void *allocate(size_t size, void *user) {
  if (failAllocation == 0)
    return NULL;
  if (failAllocation > 0)
    failAllocation--;
  return malloc(size);
}
static void *reallocate(void *block, size_t size, void *user) {
  return realloc(block, size);
}
static void deallocate(void *block, void *user) { free(block); }

static bool failures(void) {
  bool passed = false;
  GLFWwindow *window = NULL;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  for (int allocation = 0; allocation < 4; allocation++) {
    failAllocation = allocation;
    window = glfwCreateWindow(64, 64, "Allocation rollback", NULL, NULL);
    failAllocation = -1;
    CHECK(!window && glfwGetError(NULL) == GLFW_OUT_OF_MEMORY);
  }
  // More failures than the shared mapping region could retain if rollback
  // leaked the two native pixel planes after an EGL context error.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 99);
  for (int attempt = 0; attempt < 20; attempt++) {
    window = glfwCreateWindow(512, 256, "Context rollback", NULL, NULL);
    CHECK(!window && glfwGetError(NULL) == GLFW_VERSION_UNAVAILABLE);
  }
  passed = true;
done:
  failAllocation = -1;
  if (window)
    glfwDestroyWindow(window);
  glfwDefaultWindowHints();
  return passed;
}

static void error(int code, const char *description) {
  logkf("GLFW error=%x %s\n", code, description);
}
static void key(GLFWwindow *window, int code, int scancode, int action,
                int mods) {
  if (code == GLFW_KEY_A && action == GLFW_PRESS)
    input.keys++;
  if (code == GLFW_KEY_A && action == GLFW_RELEASE)
    input.releases++;
  if (code == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, 1);
}
static void character(GLFWwindow *window, unsigned codepoint) {
  if (codepoint == 'a')
    input.characters++;
}
static void cursor(GLFWwindow *window, double x, double y) { input.motion++; }
static void button(GLFWwindow *window, int button, int action, int mods) {
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    input.presses++;
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    input.buttons_up++;
}
static void scroll(GLFWwindow *window, double x, double y) {
  if (y > 0)
    input.scroll++;
}
static void closeWindow(GLFWwindow *window) { input.close++; }

static void *wakeThread(void *argument) {
  const struct timespec delay = {.tv_nsec = 50000000};
  nanosleep(&delay, NULL);
  glfwPostEmptyEvent();
  return NULL;
}

static bool waiting(void) {
  bool passed = false;
  pthread_t thread = NULL;
  unsigned payload = 0xabc123;
  ipc_msg_t send = {.peer_tid = NowTaskID(),
                    .peer_generation = ipc_generation(),
                    .type = 0x47574657,
                    .size = sizeof(payload),
                    .data = &payload,
                    .flags = IPC_NOWAIT};
  CHECK(ipc_send_msg(&send) == IPC_OK);
  double before = glfwGetTime();
  glfwWaitEventsTimeout(0.03);
  CHECK(glfwGetTime() - before >= 0.025);
  CHECK(pthread_create(&thread, NULL, wakeThread, NULL) == 0);
  before = glfwGetTime();
  glfwWaitEvents();
  CHECK(glfwGetTime() - before >= 0.035 && glfwGetTime() - before < 2);
  CHECK(pthread_join(thread, NULL) == 0);
  thread = NULL;
  unsigned received = 0;
  ipc_msg_t receive = {.size = sizeof(received),
                       .data = &received,
                       .flags = IPC_NOWAIT,
                       .from_filter = IPC_ANY_TID};
  CHECK(ipc_recv_msg(&receive) == sizeof(received) && received == payload &&
        receive.type == send.type);
  passed = true;
done:
  if (thread)
    pthread_join(thread, NULL);
  return passed;
}

struct context_test {
  GLFWwindow *window;
  GLuint texture;
  bool passed;
};
static void *contextThread(void *argument) {
  struct context_test *test = argument;
  glfwMakeContextCurrent(test->window);
  unsigned char pixel[4] = {0};
  glBindTexture(GL_TEXTURE_2D, test->texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
  test->passed = glfwGetCurrentContext() == test->window && pixel[0] == 17 &&
                 pixel[1] == 34 && pixel[2] == 51 &&
                 glGetError() == GL_NO_ERROR;
  glfwMakeContextCurrent(NULL);
  return NULL;
}

static bool contexts(GLFWwindow *window) {
  bool passed = false;
  GLFWwindow *shared = NULL;
  pthread_t thread = NULL;
  GLuint texture = 0;
  glfwMakeContextCurrent(window);
  CHECK(glfwGetCurrentContext() == window);
  CHECK(glfwGetProcAddress("glCreateShader"));
  CHECK(strstr((const char *)glGetString(GL_RENDERER), "llvmpipe"));
  glfwSwapInterval(0);
  CHECK(glfwGetError(NULL) == GLFW_NO_ERROR);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const unsigned char pixel[] = {17, 34, 51, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixel);
  glFinish();
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  shared = glfwCreateWindow(64, 64, "GLFW shared context", NULL, window);
  CHECK(shared && !glfwGetWindowAttrib(shared, GLFW_VISIBLE));
  CHECK(glfwGetWindowAttrib(window, GLFW_FOCUSED));
  struct context_test test = {shared, texture, false};
  CHECK(pthread_create(&thread, NULL, contextThread, &test) == 0);
  CHECK(pthread_join(thread, NULL) == 0);
  thread = NULL;
  CHECK(test.passed && glfwGetCurrentContext() == window);
  passed = true;
done:
  if (thread)
    pthread_join(thread, NULL);
  if (shared)
    glfwDestroyWindow(shared);
  if (texture)
    glDeleteTextures(1, &texture);
  glfwDefaultWindowHints();
  return passed;
}

static bool frame(GLFWwindow *window, unsigned phase) {
  int width, height, x, y;
  glfwGetFramebufferSize(window, &width, &height);
  glfwGetWindowPos(window, &x, &y);
  unsigned char *pixels = malloc((size_t)width * height * 3);
  if (!pixels)
    return false;
  glViewport(0, 0, width, height);
  glDisable(GL_DITHER);
  glClearColor(phase ? 0.0f : 1.0f, phase ? 1.0f : 0.0f, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glScissor(width / 4, height / 4, width / 2, height / 2);
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
  uint32_t hash = 2166136261u;
  for (int row = height - 1; row >= 0; row--)
    for (int column = 0; column < width * 3; column++)
      hash = (hash ^ pixels[(size_t)row * width * 3 + column]) * 16777619u;
  free(pixels);
  if (glGetError() != GL_NO_ERROR)
    return false;
  glfwSwapBuffers(window);
  if (glfwGetError(NULL) != GLFW_NO_ERROR)
    return false;
  logkf("GLFWTEST FRAME phase=%u x=%d y=%d width=%d height=%d hash=%08x\n",
        phase, x, y, width, height, hash);
  return true;
}

int main(int argc, char **argv) {
  bool automated = argc == 2 && !strcmp(argv[1], "--test");
  rpc_endpoint_t gui;
  if (rpc_connect("gui", &gui, 0) != RPC_OK) {
    if (!automated) {
      puts("Start gui.bin before glfwtest.bin.");
      return 1;
    }
    int child = fork();
    if (child < 0)
      return 1;
    if (!child)
      return exec("gui.bin", "gui.bin");
    if (rpc_connect("gui", &gui, 30000) != RPC_OK)
      return 1;
  }
  bool passed = false;
  GLFWwindow *window = NULL;
  GLFWallocator allocator = {allocate, reallocate, deallocate, NULL};
  glfwInitAllocator(&allocator);
  glfwSetErrorCallback(error);
  CHECK(glfwInit());
  CHECK(glfwGetPlatform() == GLFW_PLATFORM_PLANTOS);
  CHECK(glfwPlatformSupported(GLFW_PLATFORM_PLANTOS));
  CHECK(waiting());
  int count;
  GLFWmonitor **monitors = glfwGetMonitors(&count);
  CHECK(monitors && count == 1);
  const GLFWvidmode *mode = glfwGetVideoMode(monitors[0]);
  CHECK(mode && mode->width > 0 && mode->height > 0);
  CHECK(failures());
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_POSITION_X, 64);
  glfwWindowHint(GLFW_POSITION_Y, 80);
  window = glfwCreateWindow(320, 200, "Plant OS GLFW 3.4", NULL, NULL);
  CHECK(window);
  CHECK(contexts(window));
  glfwSetKeyCallback(window, key);
  glfwSetCharCallback(window, character);
  glfwSetCursorPosCallback(window, cursor);
  glfwSetMouseButtonCallback(window, button);
  glfwSetScrollCallback(window, scroll);
  glfwSetWindowCloseCallback(window, closeWindow);
  CHECK(!glfwRawMouseMotionSupported());
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  CHECK(glfwGetError(NULL) == GLFW_FEATURE_UNAVAILABLE);
  CHECK(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL);
  glfwSetWindowPos(window, 84, 104);
  int x, y, width, height;
  glfwGetWindowPos(window, &x, &y);
  CHECK(x == 84 && y == 104);
  glfwGetFramebufferSize(window, &width, &height);
  CHECK(width == 320 && height == 200);
  glfwHideWindow(window);
  CHECK(!glfwGetWindowAttrib(window, GLFW_VISIBLE));
  glfwShowWindow(window);
  glfwFocusWindow(window);
  CHECK(glfwGetWindowAttrib(window, GLFW_VISIBLE));
  CHECK(!strcmp(glfwGetKeyName(GLFW_KEY_A, 0), "a"));
  CHECK(glfwGetKeyScancode(GLFW_KEY_RIGHT) == 0xcd);
  CHECK(frame(window, 0));
  double deadline = glfwGetTime() + (automated ? 60 : 3600);
  bool second = false;
  while (!glfwWindowShouldClose(window) && glfwGetTime() < deadline) {
    glfwWaitEventsTimeout(0.1);
    if (!second && input.keys && input.releases && input.characters &&
        input.motion && input.presses && input.buttons_up && input.scroll) {
      CHECK(frame(window, 1));
      second = true;
    }
  }
  if (automated)
    CHECK(second && input.close && glfwWindowShouldClose(window));
  passed = true;
done:
  if (window)
    glfwDestroyWindow(window);
  glfwTerminate();
  if (passed) {
    // Reinitialization must not retain threads, windows or context TLS.
    passed = glfwInit();
    glfwTerminate();
  }
  logkf("GLFWTEST %s\n", passed ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
