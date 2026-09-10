//========================================================================
// GLFW 3.4 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2016 Google Inc.
// Copyright (c) 2016-2017 Camilla Löwy <elmindreda@glfw.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================

// Plant OS native platform table, derived from null_init.c.
#include "internal.h"
#include <framebuffer.h>

GLFWbool _glfwConnectPlantOS(int platformID, _GLFWplatform *platform) {
  const _GLFWplatform native = {
      .platformID = GLFW_PLATFORM_PLANTOS,
      .init = _glfwInitPlantOS,
      .terminate = _glfwTerminatePlantOS,
      .getCursorPos = _glfwGetCursorPosPlantOS,
      .setCursorPos = _glfwSetCursorPosPlantOS,
      .setCursorMode = _glfwSetCursorModePlantOS,
      .setRawMouseMotion = _glfwSetRawMouseMotionPlantOS,
      .rawMouseMotionSupported = _glfwRawMouseMotionSupportedPlantOS,
      .createCursor = _glfwCreateCursorPlantOS,
      .createStandardCursor = _glfwCreateStandardCursorPlantOS,
      .destroyCursor = _glfwDestroyCursorPlantOS,
      .setCursor = _glfwSetCursorPlantOS,
      .getScancodeName = _glfwGetScancodeNamePlantOS,
      .getKeyScancode = _glfwGetKeyScancodePlantOS,
      .setClipboardString = _glfwSetClipboardStringPlantOS,
      .getClipboardString = _glfwGetClipboardStringPlantOS,
      .initJoysticks = _glfwInitJoysticksNull,
      .terminateJoysticks = _glfwTerminateJoysticksNull,
      .pollJoystick = _glfwPollJoystickNull,
      .getMappingName = _glfwGetMappingNameNull,
      .updateGamepadGUID = _glfwUpdateGamepadGUIDNull,
      .freeMonitor = _glfwFreeMonitorPlantOS,
      .getMonitorPos = _glfwGetMonitorPosPlantOS,
      .getMonitorContentScale = _glfwGetMonitorContentScalePlantOS,
      .getMonitorWorkarea = _glfwGetMonitorWorkareaPlantOS,
      .getVideoModes = _glfwGetVideoModesPlantOS,
      .getVideoMode = _glfwGetVideoModePlantOS,
      .getGammaRamp = _glfwGetGammaRampPlantOS,
      .setGammaRamp = _glfwSetGammaRampPlantOS,
      .createWindow = _glfwCreateWindowPlantOS,
      .destroyWindow = _glfwDestroyWindowPlantOS,
      .setWindowTitle = _glfwSetWindowTitlePlantOS,
      .setWindowIcon = _glfwSetWindowIconPlantOS,
      .getWindowPos = _glfwGetWindowPosPlantOS,
      .setWindowPos = _glfwSetWindowPosPlantOS,
      .getWindowSize = _glfwGetWindowSizePlantOS,
      .setWindowSize = _glfwSetWindowSizePlantOS,
      .setWindowSizeLimits = _glfwSetWindowSizeLimitsPlantOS,
      .setWindowAspectRatio = _glfwSetWindowAspectRatioPlantOS,
      .getFramebufferSize = _glfwGetFramebufferSizePlantOS,
      .getWindowFrameSize = _glfwGetWindowFrameSizePlantOS,
      .getWindowContentScale = _glfwGetWindowContentScalePlantOS,
      .iconifyWindow = _glfwIconifyWindowPlantOS,
      .restoreWindow = _glfwRestoreWindowPlantOS,
      .maximizeWindow = _glfwMaximizeWindowPlantOS,
      .showWindow = _glfwShowWindowPlantOS,
      .hideWindow = _glfwHideWindowPlantOS,
      .requestWindowAttention = _glfwRequestWindowAttentionPlantOS,
      .focusWindow = _glfwFocusWindowPlantOS,
      .setWindowMonitor = _glfwSetWindowMonitorPlantOS,
      .windowFocused = _glfwWindowFocusedPlantOS,
      .windowIconified = _glfwWindowIconifiedPlantOS,
      .windowVisible = _glfwWindowVisiblePlantOS,
      .windowMaximized = _glfwWindowMaximizedPlantOS,
      .windowHovered = _glfwWindowHoveredPlantOS,
      .framebufferTransparent = _glfwFramebufferTransparentPlantOS,
      .getWindowOpacity = _glfwGetWindowOpacityPlantOS,
      .setWindowResizable = _glfwSetWindowResizablePlantOS,
      .setWindowDecorated = _glfwSetWindowDecoratedPlantOS,
      .setWindowFloating = _glfwSetWindowFloatingPlantOS,
      .setWindowOpacity = _glfwSetWindowOpacityPlantOS,
      .setWindowMousePassthrough = _glfwSetWindowMousePassthroughPlantOS,
      .pollEvents = _glfwPollEventsPlantOS,
      .waitEvents = _glfwWaitEventsPlantOS,
      .waitEventsTimeout = _glfwWaitEventsTimeoutPlantOS,
      .postEmptyEvent = _glfwPostEmptyEventPlantOS,
      .getEGLPlatform = _glfwGetEGLPlatformPlantOS,
      .getEGLNativeDisplay = _glfwGetEGLNativeDisplayPlantOS,
      .getEGLNativeWindow = _glfwGetEGLNativeWindowPlantOS,
      .getRequiredInstanceExtensions =
          _glfwGetRequiredInstanceExtensionsPlantOS,
      .getPhysicalDevicePresentationSupport =
          _glfwGetPhysicalDevicePresentationSupportPlantOS,
      .createWindowSurface = _glfwCreateWindowSurfacePlantOS};

  *platform = native;
  return GLFW_TRUE;
}

void _glfwInputUnsupportedPlantOS(const char *feature) {
  _glfwInputError(GLFW_FEATURE_UNAVAILABLE, "Plant OS: %s is unavailable",
                  feature);
}

int _glfwInitPlantOS(void) {
  framebuffer_info_t info;
  if (framebuffer_info(&info) < 0 || !info.width || !info.height) {
    _glfwInputError(GLFW_PLATFORM_ERROR, "Plant OS: No framebuffer");
    return GLFW_FALSE;
  }
  _glfw.plantos.mode = (GLFWvidmode){info.width, info.height, 8, 8, 8, 0};
  if (!_glfwInitEventsPlantOS())
    return GLFW_FALSE;
  // Firmware provides pixels, but no physical size or refresh rate.
  _GLFWmonitor *monitor = _glfwAllocMonitor("Plant OS framebuffer", 0, 0);
  if (!monitor)
    return GLFW_FALSE;
  _glfwInputMonitor(monitor, GLFW_CONNECTED, _GLFW_INSERT_FIRST);
  return _glfw.monitorCount == 1;
}

void _glfwTerminatePlantOS(void) {
  _glfwTerminateEventsPlantOS();
  _glfwTerminateEGL();
}

void _glfwFreeMonitorPlantOS(_GLFWmonitor *monitor) {}

void _glfwGetMonitorPosPlantOS(_GLFWmonitor *monitor, int *x, int *y) {
  if (x)
    *x = 0;
  if (y)
    *y = 0;
}

void _glfwGetMonitorContentScalePlantOS(_GLFWmonitor *monitor, float *x,
                                        float *y) {
  if (x)
    *x = 1;
  if (y)
    *y = 1;
}

void _glfwGetMonitorWorkareaPlantOS(_GLFWmonitor *monitor, int *x, int *y,
                                    int *width, int *height) {
  _glfwGetMonitorPosPlantOS(monitor, x, y);
  if (width)
    *width = _glfw.plantos.mode.width;
  if (height)
    *height = _glfw.plantos.mode.height;
}

GLFWvidmode *_glfwGetVideoModesPlantOS(_GLFWmonitor *monitor, int *count) {
  GLFWvidmode *mode = _glfw_calloc(1, sizeof(*mode));
  if (!mode)
    return NULL;
  *mode = _glfw.plantos.mode;
  *count = 1;
  return mode;
}

GLFWbool _glfwGetVideoModePlantOS(_GLFWmonitor *monitor, GLFWvidmode *mode) {
  *mode = _glfw.plantos.mode;
  return GLFW_TRUE;
}

GLFWbool _glfwGetGammaRampPlantOS(_GLFWmonitor *monitor, GLFWgammaramp *ramp) {
  _glfwInputUnsupportedPlantOS("Hardware gamma ramps");
  return GLFW_FALSE;
}

void _glfwSetGammaRampPlantOS(_GLFWmonitor *monitor,
                              const GLFWgammaramp *ramp) {
  _glfwInputUnsupportedPlantOS("Hardware gamma ramps");
}
