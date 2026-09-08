// SPDX-License-Identifier: MIT
#ifndef PLANT_EGL_H
#define PLANT_EGL_H

#include <EGL/egl.h>
#include <gui.h>

/* Keep this descriptor alive until eglDestroySurface, on the GUI owner thread.
 * Coordinates describe the client area in window_get_buffer's pixel layout. */
struct plant_egl_window {
  window_t window;
  uint32_t x, y, width, height;
};

#endif
