/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Plant OS adaptation of Mesa demos 9.0.0 glxgears: the original gear geometry,
 * fixed-function lighting and display lists use native SDL3/EGL windows.
 * Source and changes are documented in UPSTREAM.md. */
#include <EGL/egl.h>
#include <GL/gl.h>
#include <SDL3/SDL.h>
#include <gui.h>
#include <math.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>

bool opengl_test(void);

static GLfloat view_rotx = 20, view_roty = 30, view_rotz;
static GLint gear1, gear2, gear3;
static GLfloat angle;

/*
 *
 *  Draw a gear wheel.  You'll probably want to call this function when
 *  building a display list since we do a lot of trig here.
 *
 *  Input:  inner_radius - radius of hole at center
 *          outer_radius - radius at center of teeth
 *          width - width of gear
 *          teeth - number of teeth
 *          tooth_depth - depth of tooth
 */
static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                 GLint teeth, GLfloat tooth_depth) {
  GLint i;
  GLfloat r0, r1, r2;
  GLfloat angle, da;
  GLfloat u, v, len;

  r0 = inner_radius;
  r1 = outer_radius - tooth_depth / 2.0;
  r2 = outer_radius + tooth_depth / 2.0;

  da = 2.0 * M_PI / teeth / 4.0;

  glShadeModel(GL_FLAT);

  glNormal3f(0.0, 0.0, 1.0);

  /* draw front face */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i <= teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
    glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
    if (i < teeth) {
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                 width * 0.5);
    }
  }
  glEnd();

  /* draw front sides of teeth */
  glBegin(GL_QUADS);
  da = 2.0 * M_PI / teeth / 4.0;
  for (i = 0; i < teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;

    glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
  }
  glEnd();

  glNormal3f(0.0, 0.0, -1.0);

  /* draw back face */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i <= teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
    if (i < teeth) {
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                 -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
    }
  }
  glEnd();

  /* draw back sides of teeth */
  glBegin(GL_QUADS);
  da = 2.0 * M_PI / teeth / 4.0;
  for (i = 0; i < teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;

    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
               -width * 0.5);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
               -width * 0.5);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
    glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
  }
  glEnd();

  /* draw outward faces of teeth */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i < teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;

    glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
    glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
    u = r2 * cos(angle + da) - r1 * cos(angle);
    v = r2 * sin(angle + da) - r1 * sin(angle);
    len = sqrt(u * u + v * v);
    u /= len;
    v /= len;
    glNormal3f(v, -u, 0.0);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
    glNormal3f(cos(angle), sin(angle), 0.0);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
               -width * 0.5);
    u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
    v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
    glNormal3f(v, -u, 0.0);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
               -width * 0.5);
    glNormal3f(cos(angle), sin(angle), 0.0);
  }

  glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
  glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);

  glEnd();

  glShadeModel(GL_SMOOTH);

  /* draw inside radius cylinder */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i <= teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glNormal3f(-cos(angle), -sin(angle), 0.0);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
  }
  glEnd();
}

static void draw(void) {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glPushMatrix();
  glRotatef(view_rotx, 1.0, 0.0, 0.0);
  glRotatef(view_roty, 0.0, 1.0, 0.0);
  glRotatef(view_rotz, 0.0, 0.0, 1.0);

  glPushMatrix();
  glTranslatef(-3.0, -2.0, 0.0);
  glRotatef(angle, 0.0, 0.0, 1.0);
  glCallList(gear1);
  glPopMatrix();

  glPushMatrix();
  glTranslatef(3.1, -2.0, 0.0);
  glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0);
  glCallList(gear2);
  glPopMatrix();

  glPushMatrix();
  glTranslatef(-3.1, 4.2, 0.0);
  glRotatef(-2.0 * angle - 25.0, 0.0, 0.0, 1.0);
  glCallList(gear3);
  glPopMatrix();

  glPopMatrix();
}

static void init(void) {
  static GLfloat pos[4] = {5.0, 5.0, 10.0, 0.0};
  static GLfloat red[4] = {0.8, 0.1, 0.0, 1.0};
  static GLfloat green[4] = {0.0, 0.8, 0.2, 1.0};
  static GLfloat blue[4] = {0.2, 0.2, 1.0, 1.0};

  glLightfv(GL_LIGHT0, GL_POSITION, pos);
  glEnable(GL_CULL_FACE);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glEnable(GL_DEPTH_TEST);
  /* make the gears */
  gear1 = glGenLists(1);
  glNewList(gear1, GL_COMPILE);
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
  gear(1.0, 4.0, 1.0, 20, 0.7);
  glEndList();

  gear2 = glGenLists(1);
  glNewList(gear2, GL_COMPILE);
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
  gear(0.5, 2.0, 2.0, 10, 0.7);
  glEndList();

  gear3 = glGenLists(1);
  glNewList(gear3, GL_COMPILE);
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
  gear(1.3, 2.0, 0.5, 10, 0.7);
  glEndList();

  glEnable(GL_NORMALIZE);
}

static void reshape(int width, int height) {
  GLfloat h = (GLfloat)height / width;
  glViewport(0, 0, width, height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-1, 1, -h, h, 5, 60);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(0, 0, -40);
}

static bool frame_checksum(int width, int height, uint32_t *hash) {
  size_t stride = (size_t)width * 3;
  unsigned char *pixels = malloc(stride * height);
  if (!pixels)
    return false;
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
  *hash = 2166136261u;
  for (int row = height - 1; row >= 0; row--)
    for (size_t x = 0; x < stride; x++)
      *hash = (*hash ^ pixels[row * stride + x]) * 16777619u;
  free(pixels);
  return glGetError() == GL_NO_ERROR;
}

int main(int argc, char **argv) {
  bool test = false, info = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--test"))
      test = true;
    else if (!strcmp(argv[i], "-info"))
      info = true;
    else {
      puts("Usage: glxgears.bin [-info] [--test]\nArrow keys rotate, space "
           "pauses, Escape exits.");
      return 1;
    }
  }
  if (test && !opengl_test())
    return 1;
  rpc_endpoint_t gui;
  if (rpc_connect(GUI_SERVICE_NAME, &gui, 0) != RPC_OK) {
    if (!test) {
      fputs("Start gui.bin, then run glxgears.bin from its terminal.\n",
            stderr);
      return 1;
    }
    int child = fork();
    if (child < 0)
      return 1;
    if (!child) {
      char program[] = "gui.bin";
      return exec(program, program);
    }
    if (rpc_connect(GUI_SERVICE_NAME, &gui, 30000) != RPC_OK)
      return 1;
  }
  if (!SDL_Init(SDL_INIT_VIDEO))
    return 1;
  if (test) {
    if (!SDL_GL_LoadLibrary("/lib/libGL.so") ||
        !SDL_GL_LoadLibrary("/lib/libGL.so")) {
      logkf("GLXGEARS FAIL explicit library load: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
    }
    SDL_GL_UnloadLibrary();
    SDL_GL_UnloadLibrary();
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_Window *window =
      SDL_CreateWindow("glxgears - llvmpipe", 300, 300, SDL_WINDOW_OPENGL);
  SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
  int status = 1;
  if (!context || !SDL_GL_SetSwapInterval(0))
    goto done;
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  const char *version = (const char *)glGetString(GL_VERSION);
  if (!renderer || !version ||
      (test && (!strstr(renderer, "llvmpipe") ||
                !SDL_GL_GetProcAddress("glCreateShader"))))
    goto done;
  logkf("GLXGEARS GL_RENDERER = %s\n", renderer);
  logkf("GLXGEARS GL_VERSION = %s\n", version);
  if (info)
    printf("GL_RENDERER = %s\nGL_VERSION = %s\n", renderer, version);
  int width, height;
  if (!SDL_GetWindowSizeInPixels(window, &width, &height))
    goto done;
  init();
  reshape(width, height);
  bool running = true, animate = true;
  unsigned frames = 0, phase = 0;
  uint64_t last = monotonic_ns(), rate_start = last;
  while (running) {
    uint64_t now = monotonic_ns();
    if (!test && animate)
      angle = fmodf(angle + 70.0f * ((now - last) / 1000000000.0), 3600.0f);
    last = now;
    draw();
    uint32_t hash = 0;
    if (test && !frame_checksum(width, height, &hash))
      goto done;
    if (glGetError() != GL_NO_ERROR || !SDL_GL_SwapWindow(window))
      goto done;
    if (test) {
      int x, y;
      SDL_GetWindowPosition(window, &x, &y);
      logkf("GLXGEARS FRAME phase=%u x=%d y=%d width=%d height=%d hash=%08x\n",
            phase, x, y, width, height, hash);
    }
    frames++;
    now = monotonic_ns();
    if (now - rate_start >= 5000000000ull) {
      double seconds = (now - rate_start) / 1000000000.0;
      printf("%u frames in %.1f seconds = %.3f FPS\n", frames, seconds,
             frames / seconds);
      logkf("GLXGEARS %u frames in %.1f seconds = %.3f FPS\n", frames, seconds,
            frames / seconds);
      rate_start = now;
      frames = 0;
    }
    uint64_t deadline = monotonic_ns() + 60000000000ull;
    bool advance = false;
    do {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT)
          running = false;
        if (event.type != SDL_EVENT_KEY_DOWN)
          continue;
        switch (event.key.key) {
        case SDLK_ESCAPE:
          running = false;
          break;
        case SDLK_LEFT:
          view_roty += 5;
          break;
        case SDLK_RIGHT:
          view_roty -= 5;
          advance = true;
          break;
        case SDLK_UP:
          view_rotx += 5;
          break;
        case SDLK_DOWN:
          view_rotx -= 5;
          break;
        case SDLK_SPACE:
          animate = !animate;
          break;
        default:
          break;
        }
      }
      if (!test || advance || !running)
        break;
      if (monotonic_ns() >= deadline)
        goto done;
      SDL_Delay(10);
    } while (true);
    if (test && running) {
      if (phase++ != 0)
        goto done;
      angle = 37;
    }
  }
  if (test && phase != 1)
    goto done;
  glDeleteLists(gear1, 1);
  glDeleteLists(gear2, 1);
  glDeleteLists(gear3, 1);
  status = 0;
done:
  if (status)
    logkf("GLXGEARS FAIL SDL=%s EGL=%x\n", SDL_GetError(), eglGetError());
  if (context)
    SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (!status && test)
    logkf("GLXGEARS PASS\n");
  return status;
}
