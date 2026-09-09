// SPDX-License-Identifier: MIT
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <futex.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

bool opengl_test(void);

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      logkf("OPENGL FAIL line=%d %s EGL=%x GL=%x\n", __LINE__, #condition,     \
            eglGetError(), glGetError());                                      \
      goto done;                                                               \
    }                                                                          \
  } while (0)

static bool pixel(unsigned char r, unsigned char g, unsigned char b) {
  unsigned char value[4] = {0};
  glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value);
  if (value[0] != r || value[1] != g || value[2] != b || value[3] != 255) {
    logkf("OPENGL pixel=%u,%u,%u,%u expected=%u,%u,%u,255\n", value[0],
          value[1], value[2], value[3], r, g, b);
    return false;
  }
  return glGetError() == GL_NO_ERROR;
}

struct shared_test {
  EGLDisplay display;
  EGLContext owner, context;
  EGLSurface surface, owner_surface;
  GLuint texture;
  bool passed;
};

static void *shared_worker(void *argument) {
  struct shared_test *test = argument;
  CHECK(!eglMakeCurrent(test->display, test->owner_surface, test->owner_surface,
                        test->owner));
  CHECK(eglGetError() == EGL_BAD_ACCESS);
  CHECK(eglMakeCurrent(test->display, test->surface, test->surface,
                       test->context));
  CHECK(glIsTexture(test->texture));
  const unsigned char green[4] = {0, 255, 0, 255};
  unsigned char original[4] = {0};
  glBindTexture(GL_TEXTURE_2D, test->texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, original);
  CHECK(original[0] == 255 && original[1] == 0 && original[2] == 0 &&
        original[3] == 255);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  green);
  glClearColor(0, 1, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  CHECK(eglWaitClient());
  CHECK(pixel(0, 255, 0));
  test->passed = true;
done:
  eglReleaseThread();
  return NULL;
}

struct raster_test {
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
  uint32_t *start;
  unsigned seed;
  bool passed;
};

static void *raster_worker(void *argument) {
  struct raster_test *test = argument;
  unsigned char *pixels = malloc(128 * 128 * 4);
  CHECK(pixels);
  CHECK(eglMakeCurrent(test->display, test->surface, test->surface,
                       test->context));
  while (!__atomic_load_n(test->start, __ATOMIC_ACQUIRE))
    os_futex_wait(test->start, 0, UINT64_MAX);
  glEnable(GL_SCISSOR_TEST);
  for (unsigned round = 0; round < 4; round++) {
    /* Flush independent strips without waiting between scenes. Four contexts
     * share the raster pool; readback checks ordering and scene reuse. */
    for (unsigned row = 0; row < 128; row++) {
      glScissor(0, row, 128, 1);
      glClearColor(((row + round * 17) & 255) / 255.0f,
                    test->seed / 255.0f, (255 - row) / 255.0f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glFlush();
    }
    glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (unsigned row = 0; row < 128; row++) {
      for (unsigned x = 0; x < 128; x++) {
        const unsigned char *pixel = pixels + (row * 128 + x) * 4;
        CHECK(pixel[0] == ((row + round * 17) & 255) &&
              pixel[1] == test->seed && pixel[2] == 255 - row &&
              pixel[3] == 255);
      }
    }
    CHECK(glGetError() == GL_NO_ERROR);
  }
  glClear(GL_COLOR_BUFFER_BIT);
  glFlush();
  test->passed = true;
done:
  eglReleaseThread();
  free(pixels);
  return NULL;
}

static bool queued_scenes(EGLDisplay display, EGLConfig config) {
  struct raster_test tests[4] = {0};
  pthread_t threads[4];
  uint32_t start = 0;
  unsigned created = 0;
  const EGLint dimensions[] = {EGL_WIDTH, 128, EGL_HEIGHT, 128, EGL_NONE};
  for (unsigned i = 0; i < 4; i++) {
    tests[i] = (struct raster_test){
        .display = display,
        .context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL),
        .surface = eglCreatePbufferSurface(display, config, dimensions),
        .start = &start,
        .seed = 37 * (i + 1),
    };
    if (tests[i].context == EGL_NO_CONTEXT ||
        tests[i].surface == EGL_NO_SURFACE ||
        pthread_create(&threads[i], NULL, raster_worker, &tests[i]))
      break;
    created++;
  }
  __atomic_store_n(&start, 1, __ATOMIC_RELEASE);
  os_futex_wake(&start, INT_MAX);
  bool passed = created == 4;
  for (unsigned i = 0; i < created; i++) {
    passed &= pthread_join(threads[i], NULL) == 0;
    passed &= tests[i].passed;
  }
  for (unsigned i = 0; i < 4; i++) {
    if (tests[i].context != EGL_NO_CONTEXT)
      passed &= eglDestroyContext(display, tests[i].context);
    if (tests[i].surface != EGL_NO_SURFACE)
      passed &= eglDestroySurface(display, tests[i].surface);
  }
  return passed;
}

bool opengl_test(void) {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLContext context = EGL_NO_CONTEXT, shared = EGL_NO_CONTEXT,
             core = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE, second = EGL_NO_SURFACE;
  GLuint program = 0, vertex = 0, fragment = 0, texture = 0;
  bool passed = false;
  CHECK(display != EGL_NO_DISPLAY);
  CHECK(eglInitialize(display, NULL, NULL));
  CHECK(eglBindAPI(EGL_OPENGL_API));
  EGLConfig config;
  EGLint count;
  const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
      EGL_OPENGL_BIT,   EGL_DEPTH_SIZE,  24,
      EGL_NONE};
  CHECK(eglChooseConfig(display, config_attributes, &config, 1, &count) &&
        count == 1);
  const EGLint bad_version[] = {EGL_CONTEXT_MAJOR_VERSION, 99, EGL_NONE};
  CHECK(eglCreateContext(display, config, EGL_NO_CONTEXT, bad_version) ==
        EGL_NO_CONTEXT);
  CHECK(eglGetError() == EGL_BAD_MATCH);
  const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 2,
                               EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
  CHECK(context != EGL_NO_CONTEXT);
  const EGLint dimensions[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE};
  surface = eglCreatePbufferSurface(display, config, dimensions);
  second = eglCreatePbufferSurface(display, config, dimensions);
  CHECK(surface != EGL_NO_SURFACE && second != EGL_NO_SURFACE);
  CHECK(eglMakeCurrent(display, surface, surface, context));
  CHECK(strstr((const char *)glGetString(GL_RENDERER), "llvmpipe"));
  CHECK(eglGetProcAddress("glCreateShader"));
  glClearColor(1, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  CHECK(eglWaitClient() && pixel(255, 0, 0));
  CHECK(queued_scenes(display, config));

  const char *vertex_source =
      "#version 120\nvoid main() { gl_Position = gl_Vertex; }";
  const char *fragment_source = "#version 120\nvoid main() { gl_FragColor = "
                                "vec4(0.25, 0.5, 0.75, 1.0); }";
  vertex = glCreateShader(GL_VERTEX_SHADER);
  fragment = glCreateShader(GL_FRAGMENT_SHADER);
  CHECK(vertex && fragment);
  glShaderSource(vertex, 1, &vertex_source, NULL);
  glShaderSource(fragment, 1, &fragment_source, NULL);
  glCompileShader(vertex);
  glCompileShader(fragment);
  GLint compiled;
  glGetShaderiv(vertex, GL_COMPILE_STATUS, &compiled);
  CHECK(compiled);
  glGetShaderiv(fragment, GL_COMPILE_STATUS, &compiled);
  CHECK(compiled);
  program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &compiled);
  CHECK(compiled);
  glUseProgram(program);
  glViewport(0, 0, 32, 32);
  glBegin(GL_TRIANGLES);
  glVertex2f(-1, -1);
  glVertex2f(3, -1);
  glVertex2f(-1, 3);
  glEnd();
  CHECK(pixel(64, 128, 191));
  glUseProgram(0);

  const unsigned char red[4] = {255, 0, 0, 255};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               red);
  CHECK(glGetError() == GL_NO_ERROR);
  shared = eglCreateContext(display, config, context, attributes);
  CHECK(shared != EGL_NO_CONTEXT);
  struct shared_test test = {.display = display,
                             .owner = context,
                             .context = shared,
                             .surface = second,
                             .owner_surface = surface,
                             .texture = texture};
  pthread_t worker;
  CHECK(!pthread_create(&worker, NULL, shared_worker, &test));
  CHECK(!pthread_join(worker, NULL));
  CHECK(test.passed && eglGetCurrentContext() == context);
  CHECK(pixel(64, 128, 191));
  unsigned char updated[4] = {0};
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, updated);
  CHECK(updated[0] == 0 && updated[1] == 255 && updated[2] == 0 &&
        updated[3] == 255);
  CHECK(eglDestroyContext(display, shared));
  shared = EGL_NO_CONTEXT;
  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  texture = program = vertex = fragment = 0;

  const EGLint core_attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                    3,
                                    EGL_CONTEXT_MINOR_VERSION,
                                    3,
                                    EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                    EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                    EGL_NONE};
  core = eglCreateContext(display, config, EGL_NO_CONTEXT, core_attributes);
  CHECK(core != EGL_NO_CONTEXT);
  CHECK(eglMakeCurrent(display, second, second, core));
  GLint profile = 0;
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
  CHECK(profile == GL_CONTEXT_CORE_PROFILE_BIT);
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  CHECK(pixel(0, 0, 255));
  CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, core));
  CHECK(glGetError() == GL_NO_ERROR);
  CHECK(eglMakeCurrent(display, surface, surface, context));
  CHECK(eglDestroyContext(display, core));
  core = EGL_NO_CONTEXT;
  CHECK(eglDestroySurface(display, second));
  second = EGL_NO_SURFACE;

  /* EGL destruction and termination retain objects bound to this thread. */
  CHECK(eglDestroyContext(display, context));
  context = EGL_NO_CONTEXT;
  CHECK(eglDestroySurface(display, surface));
  surface = EGL_NO_SURFACE;
  CHECK(eglTerminate(display));
  glClearColor(1, 1, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  CHECK(pixel(255, 255, 0));
  CHECK(eglInitialize(display, NULL, NULL));
  CHECK(
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
  CHECK(eglTerminate(display));
  CHECK(eglInitialize(display, NULL, NULL));
  passed = true;
done:
  if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
    glDeleteTextures(1, &texture);
    if (program)
      glDeleteProgram(program);
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
  }
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (shared != EGL_NO_CONTEXT)
    eglDestroyContext(display, shared);
  if (core != EGL_NO_CONTEXT)
    eglDestroyContext(display, core);
  if (context != EGL_NO_CONTEXT)
    eglDestroyContext(display, context);
  if (surface != EGL_NO_SURFACE)
    eglDestroySurface(display, surface);
  if (second != EGL_NO_SURFACE)
    eglDestroySurface(display, second);
  eglTerminate(display);
  eglReleaseThread();
  if (passed)
    logkf("OPENGL TEST PASS\n");
  return passed;
}
