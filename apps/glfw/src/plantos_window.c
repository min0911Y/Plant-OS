// SPDX-License-Identifier: Zlib
#include "internal.h"
#include <limits.h>

GLFWbool _glfwQueryWindowPlantOS(_GLFWwindow *window,
                                 gui_window_state_t *state) {
  if (window_get_state(window->plantos.native.window, state) != 0) {
    _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: Cannot query window");
    return GLFW_FALSE;
  }
  state->x += _GLFW_PLANT_BORDER;
  state->y += _GLFW_PLANT_TITLE;
  state->cursor_x -= _GLFW_PLANT_BORDER;
  state->cursor_y -= _GLFW_PLANT_TITLE;
  if (state->cursor_x < 0 || state->cursor_y < 0 ||
      state->cursor_x >= window->plantos.native.width ||
      state->cursor_y >= window->plantos.native.height)
    state->flags &= ~GUI_WINDOW_HOVERED;
  return GLFW_TRUE;
}

void _glfwSyncWindowPlantOS(_GLFWwindow *window) {
  gui_window_state_t state;
  if (!_glfwQueryWindowPlantOS(window, &state))
    return;
  gui_window_state_t previous = window->plantos.state;
  window->plantos.state = state;
  unsigned changed = previous.flags ^ state.flags;
  if (changed & GUI_WINDOW_FOCUSED) {
    window->plantos.prefix[0] = window->plantos.prefix[1] = 0;
    if (!(state.flags & GUI_WINDOW_FOCUSED))
      window->plantos.button = 0;
    _glfwInputWindowFocus(window, (state.flags & GUI_WINDOW_FOCUSED) != 0);
  }
  if (changed & GUI_WINDOW_HOVERED)
    _glfwInputCursorEnter(window, (state.flags & GUI_WINDOW_HOVERED) != 0);
  if (state.x != previous.x || state.y != previous.y)
    _glfwInputWindowPos(window, state.x, state.y);
}

static void controlWindow(_GLFWwindow *window,
                          enum gui_window_control operation, int x, int y) {
  if (window_control(window->plantos.native.window, operation, x, y) != 0) {
    _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: Window control failed");
    return;
  }
  _glfwSyncWindowPlantOS(window);
}

static void swapInterval(int interval) {
  if (interval != 0) {
    _glfwInputUnsupportedPlantOS("Vertical synchronization");
    return;
  }
  if (!eglSwapInterval(_glfw.egl.display, interval))
    _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: Cannot set swap interval");
}

GLFWbool _glfwCreateWindowPlantOS(_GLFWwindow *window,
                                  const _GLFWwndconfig *config,
                                  const _GLFWctxconfig *context,
                                  const _GLFWfbconfig *framebuffer) {
  if (window->monitor || config->maximized || config->floating ||
      !config->decorated || config->mousePassthrough ||
      framebuffer->transparent) {
    _glfwInputUnsupportedPlantOS("Requested window style");
    return GLFW_FALSE;
  }
  if (context->client == GLFW_OPENGL_ES_API ||
      context->source == GLFW_OSMESA_CONTEXT_API) {
    _glfwInputError(GLFW_API_UNAVAILABLE,
                    "Plant OS: Use desktop OpenGL with native EGL");
    return GLFW_FALSE;
  }
  if (config->width > INT16_MAX - 2 * _GLFW_PLANT_BORDER ||
      config->height > INT16_MAX - _GLFW_PLANT_TITLE - _GLFW_PLANT_BORDER) {
    _glfwInputError(GLFW_INVALID_VALUE,
                    "Plant OS: Window exceeds GUI dimensions");
    return GLFW_FALSE;
  }
  int x = config->xpos == GLFW_ANY_POSITION
              ? (_glfw.plantos.mode.width - config->width) / 2
              : config->xpos;
  int y = config->ypos == GLFW_ANY_POSITION
              ? (_glfw.plantos.mode.height - config->height) / 2
              : config->ypos;
  if (x < INT16_MIN + _GLFW_PLANT_BORDER || x > INT16_MAX ||
      y < INT16_MIN + _GLFW_PLANT_TITLE || y > INT16_MAX) {
    _glfwInputError(GLFW_INVALID_VALUE,
                    "Plant OS: Window position exceeds GUI range");
    return GLFW_FALSE;
  }
  window->plantos.native = (struct plant_egl_window){
      .x = _GLFW_PLANT_BORDER,
      .y = _GLFW_PLANT_TITLE,
      .width = config->width,
      .height = config->height,
  };
  // Keep the native descriptor alive until EGL has destroyed the surface.
  window->plantos.native.window = create_window(
      config->title, x - _GLFW_PLANT_BORDER, y - _GLFW_PLANT_TITLE,
      config->width + 2 * _GLFW_PLANT_BORDER,
      config->height + _GLFW_PLANT_TITLE + _GLFW_PLANT_BORDER,
      (config->visible ? 0 : GUI_CREATE_HIDDEN) |
          (config->focused ? 0 : GUI_CREATE_UNFOCUSED));
  if (!window->plantos.native.window) {
    _glfwInputError(GLFW_PLATFORM_ERROR,
                    "Plant OS: Cannot create window; start gui.bin first");
    return GLFW_FALSE;
  }
  window->resizable = GLFW_FALSE;
  window_start_recv_keyboard(window->plantos.native.window);
  rpc_endpoint_t target = _glfw.plantos.eventTarget;
  if (window_set_event_target(window->plantos.native.window, target.tid,
                              target.generation) != 0) {
    _glfwInputError(GLFW_PLATFORM_ERROR,
                    "Plant OS: Cannot subscribe to GUI events");
    return GLFW_FALSE;
  }
  if (context->client != GLFW_NO_API) {
    if (!_glfwInitEGL() ||
        !_glfwCreateContextEGL(window, context, framebuffer) ||
        !_glfwRefreshContextAttribs(window, context))
      return GLFW_FALSE;
    window->context.swapInterval = swapInterval;
  }
  if (!_glfwQueryWindowPlantOS(window, &window->plantos.state))
    return GLFW_FALSE;
  return GLFW_TRUE;
}

void _glfwDestroyWindowPlantOS(_GLFWwindow *window) {
  if (window->context.destroy)
    window->context.destroy(window);
  if (window->plantos.native.window)
    close_window(window->plantos.native.window);
  window->plantos.native.window = NULL;
}

void _glfwSetWindowTitlePlantOS(_GLFWwindow *window, const char *title) {
  if (window_set_title(window->plantos.native.window, title) != 0)
    _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: Cannot set window title");
}

void _glfwGetWindowPosPlantOS(_GLFWwindow *window, int *x, int *y) {
  gui_window_state_t state;
  if (!_glfwQueryWindowPlantOS(window, &state))
    return;
  if (x)
    *x = state.x;
  if (y)
    *y = state.y;
}

void _glfwSetWindowPosPlantOS(_GLFWwindow *window, int x, int y) {
  if (x < INT16_MIN + _GLFW_PLANT_BORDER || x > INT16_MAX ||
      y < INT16_MIN + _GLFW_PLANT_TITLE || y > INT16_MAX) {
    _glfwInputError(GLFW_INVALID_VALUE,
                    "Plant OS: Window position exceeds GUI range");
    return;
  }
  controlWindow(window, GUI_WINDOW_MOVE, x - _GLFW_PLANT_BORDER,
                y - _GLFW_PLANT_TITLE);
}

void _glfwGetWindowSizePlantOS(_GLFWwindow *window, int *width, int *height) {
  if (width)
    *width = window->plantos.native.width;
  if (height)
    *height = window->plantos.native.height;
}

void _glfwGetFramebufferSizePlantOS(_GLFWwindow *window, int *width,
                                    int *height) {
  _glfwGetWindowSizePlantOS(window, width, height);
}

void _glfwGetWindowFrameSizePlantOS(_GLFWwindow *window, int *left, int *top,
                                    int *right, int *bottom) {
  if (left)
    *left = _GLFW_PLANT_BORDER;
  if (top)
    *top = _GLFW_PLANT_TITLE;
  if (right)
    *right = _GLFW_PLANT_BORDER;
  if (bottom)
    *bottom = _GLFW_PLANT_BORDER;
}

void _glfwGetWindowContentScalePlantOS(_GLFWwindow *window, float *x,
                                       float *y) {
  if (x)
    *x = 1;
  if (y)
    *y = 1;
}

void _glfwShowWindowPlantOS(_GLFWwindow *window) {
  // The GLFW core applies focusOnShow after this callback.
  controlWindow(window, GUI_WINDOW_SHOW, 0, 0);
}
void _glfwHideWindowPlantOS(_GLFWwindow *window) {
  controlWindow(window, GUI_WINDOW_HIDE, 0, 0);
}
void _glfwFocusWindowPlantOS(_GLFWwindow *window) {
  controlWindow(window, GUI_WINDOW_FOCUS, 0, 0);
}

GLFWbool _glfwWindowFocusedPlantOS(_GLFWwindow *window) {
  gui_window_state_t state;
  return _glfwQueryWindowPlantOS(window, &state) &&
         (state.flags & GUI_WINDOW_FOCUSED);
}
GLFWbool _glfwWindowVisiblePlantOS(_GLFWwindow *window) {
  gui_window_state_t state;
  return _glfwQueryWindowPlantOS(window, &state) &&
         (state.flags & GUI_WINDOW_VISIBLE);
}
GLFWbool _glfwWindowHoveredPlantOS(_GLFWwindow *window) {
  gui_window_state_t state;
  return _glfwQueryWindowPlantOS(window, &state) &&
         (state.flags & GUI_WINDOW_HOVERED);
}
GLFWbool _glfwWindowIconifiedPlantOS(_GLFWwindow *window) { return GLFW_FALSE; }
GLFWbool _glfwWindowMaximizedPlantOS(_GLFWwindow *window) { return GLFW_FALSE; }
GLFWbool _glfwFramebufferTransparentPlantOS(_GLFWwindow *window) {
  return GLFW_FALSE;
}
float _glfwGetWindowOpacityPlantOS(_GLFWwindow *window) { return 1; }

void _glfwSetWindowSizePlantOS(_GLFWwindow *window, int width, int height) {
  if (width != window->plantos.native.width ||
      height != window->plantos.native.height)
    _glfwInputUnsupportedPlantOS("Window resizing");
}
void _glfwSetWindowResizablePlantOS(_GLFWwindow *window, GLFWbool enabled) {
  window->resizable = GLFW_FALSE;
  if (enabled)
    _glfwInputUnsupportedPlantOS("Window resizing");
}
void _glfwSetWindowDecoratedPlantOS(_GLFWwindow *window, GLFWbool enabled) {
  window->decorated = GLFW_TRUE;
  if (!enabled)
    _glfwInputUnsupportedPlantOS("Undecorated windows");
}
void _glfwSetWindowFloatingPlantOS(_GLFWwindow *window, GLFWbool enabled) {
  window->floating = GLFW_FALSE;
  if (enabled)
    _glfwInputUnsupportedPlantOS("Floating windows");
}
void _glfwSetWindowMousePassthroughPlantOS(_GLFWwindow *window,
                                           GLFWbool enabled) {
  window->mousePassthrough = GLFW_FALSE;
  if (enabled)
    _glfwInputUnsupportedPlantOS("Mouse passthrough");
}
void _glfwSetWindowOpacityPlantOS(_GLFWwindow *window, float opacity) {
  if (opacity != 1)
    _glfwInputUnsupportedPlantOS("Window opacity");
}
void _glfwSetWindowSizeLimitsPlantOS(_GLFWwindow *window, int a, int b, int c,
                                     int d) {
  _glfwInputUnsupportedPlantOS("Window size limits");
}
void _glfwSetWindowAspectRatioPlantOS(_GLFWwindow *window, int n, int d) {
  _glfwInputUnsupportedPlantOS("Window aspect ratio constraints");
}
void _glfwSetWindowMonitorPlantOS(_GLFWwindow *window, _GLFWmonitor *monitor,
                                  int x, int y, int width, int height,
                                  int rate) {
  if (monitor) {
    _glfwInputUnsupportedPlantOS("Fullscreen windows");
    return;
  }
  _glfwSetWindowSizePlantOS(window, width, height);
  _glfwSetWindowPosPlantOS(window, x, y);
}
void _glfwIconifyWindowPlantOS(_GLFWwindow *window) {
  _glfwInputUnsupportedPlantOS("Window iconification");
}
void _glfwRestoreWindowPlantOS(_GLFWwindow *window) {
  // There is no iconified or maximized state to restore.
}
void _glfwMaximizeWindowPlantOS(_GLFWwindow *window) {
  _glfwInputUnsupportedPlantOS("Window maximization");
}
void _glfwRequestWindowAttentionPlantOS(_GLFWwindow *window) {
  _glfwInputUnsupportedPlantOS("Window attention requests");
}
void _glfwSetWindowIconPlantOS(_GLFWwindow *window, int count,
                               const GLFWimage *images) {
  _glfwInputUnsupportedPlantOS("Window icons");
}

EGLenum _glfwGetEGLPlatformPlantOS(EGLint **attribs) { return 0; }
EGLNativeDisplayType _glfwGetEGLNativeDisplayPlantOS(void) {
  return EGL_DEFAULT_DISPLAY;
}
EGLNativeWindowType _glfwGetEGLNativeWindowPlantOS(_GLFWwindow *window) {
  return &window->plantos.native;
}

// GLFW's Vulkan API remains queryable, but native WSI is not exposed yet.
void _glfwGetRequiredInstanceExtensionsPlantOS(char **extensions) {}
GLFWbool _glfwGetPhysicalDevicePresentationSupportPlantOS(
    VkInstance instance, VkPhysicalDevice device, uint32_t family) {
  return GLFW_FALSE;
}
VkResult _glfwCreateWindowSurfacePlantOS(VkInstance instance,
                                         _GLFWwindow *window,
                                         const VkAllocationCallbacks *allocator,
                                         VkSurfaceKHR *surface) {
  _glfwInputError(GLFW_API_UNAVAILABLE,
                  "Plant OS: GLFW Vulkan WSI is unavailable");
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}
