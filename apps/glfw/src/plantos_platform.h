// SPDX-License-Identifier: Zlib
// Plant OS native backend for GLFW 3.4.
#pragma once
#include <plant_egl.h>
#include <pthread.h>
#include <rpc_wire.h>

#define GLFW_PLANTOS_WINDOW_STATE _GLFWwindowPlantOS plantos;
#define GLFW_PLANTOS_LIBRARY_WINDOW_STATE _GLFWlibraryPlantOS plantos;

enum { _GLFW_PLANT_BORDER = 4, _GLFW_PLANT_TITLE = 24 };

typedef struct {
  struct plant_egl_window native;
  gui_window_state_t state;
  unsigned char prefix[2];
  unsigned char button;
  unsigned lockMods;
} _GLFWwindowPlantOS;

typedef struct {
  GLFWvidmode mode;
  pthread_t eventThread;
  rpc_endpoint_t eventTarget;
  uint32_t ready, pending, stop;
} _GLFWlibraryPlantOS;

void _glfwInputUnsupportedPlantOS(const char *feature);
GLFWbool _glfwQueryWindowPlantOS(_GLFWwindow *window,
                                 gui_window_state_t *state);
void _glfwSyncWindowPlantOS(_GLFWwindow *window);
GLFWbool _glfwInitEventsPlantOS(void);
void _glfwTerminateEventsPlantOS(void);

GLFWbool _glfwConnectPlantOS(int platformID, _GLFWplatform *platform);
int _glfwInitPlantOS(void);
void _glfwTerminatePlantOS(void);

void _glfwFreeMonitorPlantOS(_GLFWmonitor *monitor);
void _glfwGetMonitorPosPlantOS(_GLFWmonitor *monitor, int *xpos, int *ypos);
void _glfwGetMonitorContentScalePlantOS(_GLFWmonitor *monitor, float *xscale,
                                        float *yscale);
void _glfwGetMonitorWorkareaPlantOS(_GLFWmonitor *monitor, int *xpos, int *ypos,
                                    int *width, int *height);
GLFWvidmode *_glfwGetVideoModesPlantOS(_GLFWmonitor *monitor, int *found);
GLFWbool _glfwGetVideoModePlantOS(_GLFWmonitor *monitor, GLFWvidmode *mode);
GLFWbool _glfwGetGammaRampPlantOS(_GLFWmonitor *monitor, GLFWgammaramp *ramp);
void _glfwSetGammaRampPlantOS(_GLFWmonitor *monitor, const GLFWgammaramp *ramp);

GLFWbool _glfwCreateWindowPlantOS(_GLFWwindow *window,
                                  const _GLFWwndconfig *wndconfig,
                                  const _GLFWctxconfig *ctxconfig,
                                  const _GLFWfbconfig *fbconfig);
void _glfwDestroyWindowPlantOS(_GLFWwindow *window);
void _glfwSetWindowTitlePlantOS(_GLFWwindow *window, const char *title);
void _glfwSetWindowIconPlantOS(_GLFWwindow *window, int count,
                               const GLFWimage *images);
void _glfwSetWindowMonitorPlantOS(_GLFWwindow *window, _GLFWmonitor *monitor,
                                  int xpos, int ypos, int width, int height,
                                  int refreshRate);
void _glfwGetWindowPosPlantOS(_GLFWwindow *window, int *xpos, int *ypos);
void _glfwSetWindowPosPlantOS(_GLFWwindow *window, int xpos, int ypos);
void _glfwGetWindowSizePlantOS(_GLFWwindow *window, int *width, int *height);
void _glfwSetWindowSizePlantOS(_GLFWwindow *window, int width, int height);
void _glfwSetWindowSizeLimitsPlantOS(_GLFWwindow *window, int minwidth,
                                     int minheight, int maxwidth,
                                     int maxheight);
void _glfwSetWindowAspectRatioPlantOS(_GLFWwindow *window, int n, int d);
void _glfwGetFramebufferSizePlantOS(_GLFWwindow *window, int *width,
                                    int *height);
void _glfwGetWindowFrameSizePlantOS(_GLFWwindow *window, int *left, int *top,
                                    int *right, int *bottom);
void _glfwGetWindowContentScalePlantOS(_GLFWwindow *window, float *xscale,
                                       float *yscale);
void _glfwIconifyWindowPlantOS(_GLFWwindow *window);
void _glfwRestoreWindowPlantOS(_GLFWwindow *window);
void _glfwMaximizeWindowPlantOS(_GLFWwindow *window);
GLFWbool _glfwWindowMaximizedPlantOS(_GLFWwindow *window);
GLFWbool _glfwWindowHoveredPlantOS(_GLFWwindow *window);
GLFWbool _glfwFramebufferTransparentPlantOS(_GLFWwindow *window);
void _glfwSetWindowResizablePlantOS(_GLFWwindow *window, GLFWbool enabled);
void _glfwSetWindowDecoratedPlantOS(_GLFWwindow *window, GLFWbool enabled);
void _glfwSetWindowFloatingPlantOS(_GLFWwindow *window, GLFWbool enabled);
void _glfwSetWindowMousePassthroughPlantOS(_GLFWwindow *window,
                                           GLFWbool enabled);
float _glfwGetWindowOpacityPlantOS(_GLFWwindow *window);
void _glfwSetWindowOpacityPlantOS(_GLFWwindow *window, float opacity);
void _glfwSetRawMouseMotionPlantOS(_GLFWwindow *window, GLFWbool enabled);
GLFWbool _glfwRawMouseMotionSupportedPlantOS(void);
void _glfwShowWindowPlantOS(_GLFWwindow *window);
void _glfwRequestWindowAttentionPlantOS(_GLFWwindow *window);
void _glfwHideWindowPlantOS(_GLFWwindow *window);
void _glfwFocusWindowPlantOS(_GLFWwindow *window);
GLFWbool _glfwWindowFocusedPlantOS(_GLFWwindow *window);
GLFWbool _glfwWindowIconifiedPlantOS(_GLFWwindow *window);
GLFWbool _glfwWindowVisiblePlantOS(_GLFWwindow *window);
void _glfwPollEventsPlantOS(void);
void _glfwWaitEventsPlantOS(void);
void _glfwWaitEventsTimeoutPlantOS(double timeout);
void _glfwPostEmptyEventPlantOS(void);
void _glfwGetCursorPosPlantOS(_GLFWwindow *window, double *xpos, double *ypos);
void _glfwSetCursorPosPlantOS(_GLFWwindow *window, double x, double y);
void _glfwSetCursorModePlantOS(_GLFWwindow *window, int mode);
GLFWbool _glfwCreateCursorPlantOS(_GLFWcursor *cursor, const GLFWimage *image,
                                  int xhot, int yhot);
GLFWbool _glfwCreateStandardCursorPlantOS(_GLFWcursor *cursor, int shape);
void _glfwDestroyCursorPlantOS(_GLFWcursor *cursor);
void _glfwSetCursorPlantOS(_GLFWwindow *window, _GLFWcursor *cursor);
void _glfwSetClipboardStringPlantOS(const char *string);
const char *_glfwGetClipboardStringPlantOS(void);
const char *_glfwGetScancodeNamePlantOS(int scancode);
int _glfwGetKeyScancodePlantOS(int key);

EGLenum _glfwGetEGLPlatformPlantOS(EGLint **attribs);
EGLNativeDisplayType _glfwGetEGLNativeDisplayPlantOS(void);
EGLNativeWindowType _glfwGetEGLNativeWindowPlantOS(_GLFWwindow *window);

void _glfwGetRequiredInstanceExtensionsPlantOS(char **extensions);
GLFWbool _glfwGetPhysicalDevicePresentationSupportPlantOS(
    VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
VkResult _glfwCreateWindowSurfacePlantOS(VkInstance instance,
                                         _GLFWwindow *window,
                                         const VkAllocationCallbacks *allocator,
                                         VkSurfaceKHR *surface);
