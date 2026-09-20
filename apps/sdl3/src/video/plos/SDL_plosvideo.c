/*
  Simple DirectMedia Layer
  Copyright (C) 2017 BlackBerry Limited

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* Plant OS backend: native GUI RPC windows and shared pixel/event mappings. */
#include "../../SDL_internal.h"
#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
#include <framebuffer.h>
#include <gui.h>
#ifdef SDL_VIDEO_OPENGL_EGL
#include "../SDL_egl_c.h"
#include <plant_egl.h>
#endif
#ifdef SDL_VIDEO_VULKAN
#include "../SDL_vulkan_internal.h"
#include <plant_vulkan.h>
#endif

/* GUI window decorations surround the SDL client area. */
enum { BORDER = 4, TITLE = 24 };

struct SDL_WindowData {
  window_t handle;
  int width, height;
  Uint8 prefix[2];
  Uint8 button;
  unsigned mouse_mode;
#ifdef SDL_VIDEO_OPENGL_EGL
  struct plant_egl_window egl_window;
  EGLSurface egl_surface;
#endif
};

static bool PLOS_MouseMode(SDL_Window *window, unsigned mask, unsigned flags) {
  if (!window || !window->internal)
    return true;
  SDL_WindowData *data = window->internal;
  unsigned mode = (data->mouse_mode & ~mask) | flags;
  gui_window_state_t state;
  if (window_get_state(data->handle, &state) != 0)
    return SDL_SetError("Cannot query GUI mouse owner");
  if ((state.flags & GUI_WINDOW_FOCUSED) &&
      window_control(data->handle, GUI_WINDOW_MOUSE_MODE, mode, 0) != 0)
    return SDL_SetError("Cannot set GUI mouse mode");
  data->mouse_mode = mode;
  return true;
}

static bool PLOS_CaptureMouse(SDL_Window *window) {
  for (SDL_Window *entry = SDL_GetVideoDevice()->windows; entry;
       entry = entry->next) {
    SDL_WindowData *data = entry->internal;
    if (!data || (entry != window && !(data->mouse_mode & GUI_MOUSE_CAPTURE)))
      continue;
    if (!PLOS_MouseMode(entry, GUI_MOUSE_CAPTURE,
                        entry == window ? GUI_MOUSE_CAPTURE : 0))
      return false;
  }
  return true;
}

static bool PLOS_SetRelativeMouseMode(bool enabled) {
  return PLOS_MouseMode(SDL_GetKeyboardFocus(), GUI_MOUSE_RELATIVE,
                        enabled ? GUI_MOUSE_RELATIVE : 0);
}

static bool PLOS_SetWindowMouseGrab(SDL_VideoDevice *_this, SDL_Window *window,
                                    bool grabbed) {
  return PLOS_MouseMode(window, GUI_MOUSE_CONFINED,
                        grabbed ? GUI_MOUSE_CONFINED : 0);
}

static bool PLOS_VideoInit(SDL_VideoDevice *_this) {
  SDL_Mouse *mouse = SDL_GetMouse();
  mouse->CaptureMouse = PLOS_CaptureMouse;
  mouse->SetRelativeMouseMode = PLOS_SetRelativeMouseMode;
  /* CPU window surfaces already have a direct GUI mapping. */
  SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0",
                          SDL_HINT_DEFAULT);
  framebuffer_info_t info;
  if (framebuffer_info(&info) < 0)
    return SDL_SetError("Plant OS display is unavailable");
  SDL_DisplayMode mode = {.format = SDL_PIXELFORMAT_ARGB8888,
                          .w = info.width,
                          .h = info.height,
                          .pixel_density = 1.0f};
  return SDL_AddBasicVideoDisplay(&mode) != 0;
}

static bool PLOS_GetDisplayModes(SDL_VideoDevice *_this,
                                 SDL_VideoDisplay *display) {
  return SDL_AddFullscreenDisplayMode(display, &display->desktop_mode);
}

static bool PLOS_SetDisplayMode(SDL_VideoDevice *_this,
                                SDL_VideoDisplay *display,
                                SDL_DisplayMode *mode) {
  if (mode->w != display->desktop_mode.w ||
      mode->h != display->desktop_mode.h ||
      mode->format != display->desktop_mode.format)
    return SDL_SetError("Plant OS uses a fixed desktop mode");
  return true;
}

static bool PLOS_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window,
                              SDL_PropertiesID props) {
  if (window->flags & SDL_WINDOW_METAL)
    return SDL_SetError("Plant OS has no Metal backend");
#ifndef SDL_VIDEO_OPENGL_EGL
  if (window->flags & SDL_WINDOW_OPENGL)
    return SDL_SetError("OpenGL requires the x86_64 backend");
#endif
#ifndef SDL_VIDEO_VULKAN
  if (window->flags & SDL_WINDOW_VULKAN)
    return SDL_SetError("Vulkan requires the x86_64 backend");
#endif
  if (window->w > INT16_MAX - 2 * BORDER ||
      window->h > INT16_MAX - TITLE - BORDER)
    return SDL_SetError("Window dimensions exceed the GUI protocol range");
  SDL_WindowData *data = SDL_calloc(1, sizeof(*data));
  if (!data)
    return SDL_OutOfMemory();
  data->width = window->w;
  data->height = window->h;
  data->handle = create_window(
      window->title ? window->title : "SDL3", window->x, window->y,
      window->w + 2 * BORDER, window->h + TITLE + BORDER,
      (window->flags & SDL_WINDOW_RESIZABLE) ? GUI_CREATE_RESIZABLE : 0);
  if (!data->handle) {
    SDL_free(data);
    return SDL_SetError("Cannot create a GUI window; start gui.bin first");
  }
#ifdef SDL_VIDEO_OPENGL_EGL
  if (window->flags & SDL_WINDOW_OPENGL) {
    data->egl_window = (struct plant_egl_window){
        .window = data->handle,
        .x = BORDER,
        .y = TITLE,
        .width = window->w,
        .height = window->h,
    };
    data->egl_surface = SDL_EGL_CreateSurface(_this, window, &data->egl_window);
    if (data->egl_surface == EGL_NO_SURFACE) {
      close_window(data->handle);
      SDL_free(data);
      return false;
    }
  }
#endif
  window->internal = data;
  window_start_recv_keyboard(data->handle);
  SDL_SetMouseFocus(window);
  SDL_SetKeyboardFocus(window);
  return true;
}

static bool PLOS_CreateWindowFramebuffer(SDL_VideoDevice *_this,
                                         SDL_Window *window,
                                         SDL_PixelFormat *format, void **pixels,
                                         int *pitch) {
  SDL_WindowData *data = window->internal;
  Uint32 *framebuffer = window_get_fb(data->handle);
  if (!framebuffer)
    return SDL_SetError("GUI framebuffer mapping is unavailable");
  int stride = data->width + 2 * BORDER;
  *pixels = framebuffer + TITLE * stride + BORDER;
  *pitch = stride * sizeof(*framebuffer);
  *format = SDL_PIXELFORMAT_ARGB8888;
  return true;
}

static bool PLOS_UpdateWindowFramebuffer(SDL_VideoDevice *_this,
                                         SDL_Window *window,
                                         const SDL_Rect *rects, int count) {
  SDL_WindowData *data = window->internal;
  SDL_Rect bounds = {0, 0, data->width, data->height};
  SDL_Rect damage = {0};
  for (int i = 0; i < count; i++) {
    SDL_Rect clipped;
    if (SDL_GetRectIntersection(&bounds, &rects[i], &clipped))
      SDL_GetRectUnion(&damage, &clipped, &damage);
  }
  if (!SDL_RectEmpty(&damage)) {
    unsigned x = damage.x + BORDER, y = damage.y + TITLE;
    if (window_present(data->handle, (x << 16) | y,
                       ((x + damage.w) << 16) | (y + damage.h)) != 0)
      return SDL_SetError("Cannot present the GUI window framebuffer");
  }
  return true;
}

static void PLOS_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window) {
  SDL_WindowData *data = window->internal;
  if (window_set_title(data->handle, window->title ? window->title : "") != 0)
    SDL_SetError("Cannot update the GUI window title");
}

static bool PLOS_ResizeWindow(SDL_VideoDevice *_this, SDL_Window *window,
                              int width, int height) {
  SDL_WindowData *data = window->internal;
  if (width < 32 || height < 1 || width > INT16_MAX - 2 * BORDER ||
      height > INT16_MAX - TITLE - BORDER ||
      window_resize(data->handle, width + 2 * BORDER,
                    height + TITLE + BORDER) != 0)
    return SDL_SetError("Cannot resize the GUI window");
  data->width = width;
  data->height = height;
#ifdef SDL_VIDEO_OPENGL_EGL
  data->egl_window.width = width;
  data->egl_window.height = height;
  if ((window->flags & SDL_WINDOW_OPENGL) &&
      SDL_GL_GetCurrentWindow() == window)
    _this->egl_data->eglWaitNative(EGL_CORE_NATIVE_ENGINE);
#endif
  SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, width, height);
  SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_EXPOSED, 0, 0);
  return true;
}

static void PLOS_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window) {
  PLOS_ResizeWindow(_this, window, window->pending.w, window->pending.h);
}

static void PLOS_SetWindowResizable(SDL_VideoDevice *_this, SDL_Window *window,
                                    bool enabled) {
  SDL_WindowData *data = window->internal;
  if (window_control(data->handle, GUI_WINDOW_SET_RESIZABLE, enabled, 0) != 0)
    SDL_SetError("Cannot change GUI window resizing");
}

static void PLOS_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window) {
  SDL_WindowData *data = window->internal;
  if (!data)
    return;
#ifdef SDL_VIDEO_OPENGL_EGL
  if (data->egl_surface != EGL_NO_SURFACE)
    SDL_EGL_DestroySurface(_this, data->egl_surface);
#endif
  window_stop_recv_keyboard(data->handle);
  close_window(data->handle);
  SDL_free(data);
  window->internal = NULL;
}

static const SDL_Scancode scancodes[256] = {
    [0x1e] = SDL_SCANCODE_A,
    [0x30] = SDL_SCANCODE_B,
    [0x2e] = SDL_SCANCODE_C,
    [0x20] = SDL_SCANCODE_D,
    [0x12] = SDL_SCANCODE_E,
    [0x21] = SDL_SCANCODE_F,
    [0x22] = SDL_SCANCODE_G,
    [0x23] = SDL_SCANCODE_H,
    [0x17] = SDL_SCANCODE_I,
    [0x24] = SDL_SCANCODE_J,
    [0x25] = SDL_SCANCODE_K,
    [0x26] = SDL_SCANCODE_L,
    [0x32] = SDL_SCANCODE_M,
    [0x31] = SDL_SCANCODE_N,
    [0x18] = SDL_SCANCODE_O,
    [0x19] = SDL_SCANCODE_P,
    [0x10] = SDL_SCANCODE_Q,
    [0x13] = SDL_SCANCODE_R,
    [0x1f] = SDL_SCANCODE_S,
    [0x14] = SDL_SCANCODE_T,
    [0x16] = SDL_SCANCODE_U,
    [0x2f] = SDL_SCANCODE_V,
    [0x11] = SDL_SCANCODE_W,
    [0x2d] = SDL_SCANCODE_X,
    [0x15] = SDL_SCANCODE_Y,
    [0x2c] = SDL_SCANCODE_Z,
    [0x02] = SDL_SCANCODE_1,
    [0x03] = SDL_SCANCODE_2,
    [0x04] = SDL_SCANCODE_3,
    [0x05] = SDL_SCANCODE_4,
    [0x06] = SDL_SCANCODE_5,
    [0x07] = SDL_SCANCODE_6,
    [0x08] = SDL_SCANCODE_7,
    [0x09] = SDL_SCANCODE_8,
    [0x0a] = SDL_SCANCODE_9,
    [0x0b] = SDL_SCANCODE_0,
    [0x1c] = SDL_SCANCODE_RETURN,
    [0x01] = SDL_SCANCODE_ESCAPE,
    [0x0e] = SDL_SCANCODE_BACKSPACE,
    [0x0f] = SDL_SCANCODE_TAB,
    [0x39] = SDL_SCANCODE_SPACE,
    [0x0c] = SDL_SCANCODE_MINUS,
    [0x0d] = SDL_SCANCODE_EQUALS,
    [0x1a] = SDL_SCANCODE_LEFTBRACKET,
    [0x1b] = SDL_SCANCODE_RIGHTBRACKET,
    [0x2b] = SDL_SCANCODE_BACKSLASH,
    [0x27] = SDL_SCANCODE_SEMICOLON,
    [0x28] = SDL_SCANCODE_APOSTROPHE,
    [0x29] = SDL_SCANCODE_GRAVE,
    [0x33] = SDL_SCANCODE_COMMA,
    [0x34] = SDL_SCANCODE_PERIOD,
    [0x35] = SDL_SCANCODE_SLASH,
    [0x3a] = SDL_SCANCODE_CAPSLOCK,
    [0x3b] = SDL_SCANCODE_F1,
    [0x3c] = SDL_SCANCODE_F2,
    [0x3d] = SDL_SCANCODE_F3,
    [0x3e] = SDL_SCANCODE_F4,
    [0x3f] = SDL_SCANCODE_F5,
    [0x40] = SDL_SCANCODE_F6,
    [0x41] = SDL_SCANCODE_F7,
    [0x42] = SDL_SCANCODE_F8,
    [0x43] = SDL_SCANCODE_F9,
    [0x44] = SDL_SCANCODE_F10,
    [0x57] = SDL_SCANCODE_F11,
    [0x58] = SDL_SCANCODE_F12,
    [0xd2] = SDL_SCANCODE_INSERT,
    [0xc7] = SDL_SCANCODE_HOME,
    [0xc9] = SDL_SCANCODE_PAGEUP,
    [0xd3] = SDL_SCANCODE_DELETE,
    [0xd1] = SDL_SCANCODE_PAGEDOWN,
    [0xcd] = SDL_SCANCODE_RIGHT,
    [0xcb] = SDL_SCANCODE_LEFT,
    [0xd0] = SDL_SCANCODE_DOWN,
    [0xc8] = SDL_SCANCODE_UP,
    [0x45] = SDL_SCANCODE_NUMLOCKCLEAR,
    [0xb5] = SDL_SCANCODE_KP_DIVIDE,
    [0x37] = SDL_SCANCODE_KP_MULTIPLY,
    [0x4a] = SDL_SCANCODE_KP_MINUS,
    [0x4e] = SDL_SCANCODE_KP_PLUS,
    [0x9c] = SDL_SCANCODE_KP_ENTER,
    [0x4f] = SDL_SCANCODE_KP_1,
    [0x50] = SDL_SCANCODE_KP_2,
    [0x51] = SDL_SCANCODE_KP_3,
    [0x4b] = SDL_SCANCODE_KP_4,
    [0x4c] = SDL_SCANCODE_KP_5,
    [0x4d] = SDL_SCANCODE_KP_6,
    [0x47] = SDL_SCANCODE_KP_7,
    [0x48] = SDL_SCANCODE_KP_8,
    [0x49] = SDL_SCANCODE_KP_9,
    [0x52] = SDL_SCANCODE_KP_0,
    [0x53] = SDL_SCANCODE_KP_PERIOD,
    [0x1d] = SDL_SCANCODE_LCTRL,
    [0x2a] = SDL_SCANCODE_LSHIFT,
    [0x38] = SDL_SCANCODE_LALT,
    [0x9d] = SDL_SCANCODE_RCTRL,
    [0x36] = SDL_SCANCODE_RSHIFT,
    [0xcf] = SDL_SCANCODE_END,
    [0xb8] = SDL_SCANCODE_RALT,
};

static void PLOS_MouseButtons(SDL_Window *window, unsigned state) {
  SDL_WindowData *data = window->internal;
  const Uint8 buttons[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE,
                           SDL_BUTTON_X1, SDL_BUTTON_X2};
  for (unsigned button = 0; button < SDL_arraysize(buttons); button++) {
    if ((state ^ data->button) & (1u << button))
      SDL_SendMouseButton(SDL_GetTicksNS(), window, 0, buttons[button],
                          (state & (1u << button)) != 0);
  }
  data->button = state;
}

static void PLOS_PumpEvents(SDL_VideoDevice *_this) {
  for (SDL_Window *window = _this->windows; window; window = window->next) {
    SDL_WindowData *data = window->internal;
    if (!data)
      continue;
    for (int released = 0; released < 2; released++) {
      int raw;
      while ((raw = released ? window_get_key_up_data(data->handle)
                             : window_get_key_press_data(data->handle)) >= 0) {
        if (raw == 0xe0) {
          data->prefix[released] = 0x80;
          continue;
        }
        unsigned code = (raw & 0x7f) | data->prefix[released];
        data->prefix[released] = 0;
        SDL_Scancode key = scancodes[code];
        if (key == SDL_SCANCODE_UNKNOWN)
          continue;
        SDL_SetKeyboardFocus(window);
        SDL_SendKeyboardKey(SDL_GetTicksNS(), 0, code, key, !released);
        SDL_Keymod modifiers = SDL_GetModState();
        if (released || !SDL_TextInputActive(window) ||
            (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
          continue;
        SDL_Keycode character = SDL_GetKeyFromScancode(key, modifiers, false);
        if (character >= 32 && character < 127) {
          char text[2] = {(char)character, 0};
          SDL_SendKeyboardText(text);
        }
      }
    }
    gui_event_t event;
    while (window_get_event(data->handle, &event) > 0) {
      if (event.type == GUI_EVENT_CLOSE_WINDOW) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
        continue;
      }
      SDL_SendMouseMotion(SDL_GetTicksNS(), window, 0, event.relative,
                          event.relative ? event.dx : event.x - BORDER,
                          event.relative ? event.dy : event.y - TITLE);
      if (event.wheel)
        SDL_SendMouseWheel(SDL_GetTicksNS(), window, 0, 0, event.wheel,
                           SDL_MOUSEWHEEL_NORMAL);
      PLOS_MouseButtons(window, event.buttons);
    }
    gui_window_state_t state;
    if (window_get_state(data->handle, &state) != 0)
      continue;
    PLOS_MouseButtons(window, state.buttons);
    if (state.flags & GUI_WINDOW_FOCUSED) {
      if (SDL_GetKeyboardFocus() != window) {
        SDL_SetKeyboardFocus(window);
        PLOS_MouseMode(window, 0, 0);
      }
    } else if (SDL_GetKeyboardFocus() == window) {
      SDL_SetKeyboardFocus(NULL);
    }
    if (state.requested_width != state.width ||
        state.requested_height != state.height)
      PLOS_ResizeWindow(_this, window, state.requested_width - 2 * BORDER,
                        state.requested_height - TITLE - BORDER);
  }
}

/* SDL requires this callback; window teardown owns all native resources. */
static void PLOS_VideoQuit(SDL_VideoDevice *_this) {}

static void PLOS_DeleteDevice(SDL_VideoDevice *device) { SDL_free(device); }

#ifdef SDL_VIDEO_VULKAN
static bool PLOS_Vulkan_LoadLibrary(SDL_VideoDevice *device, const char *path) {
  if (path && SDL_strcmp(path, "liblvp.so") &&
      SDL_strcmp(path, "/lib/liblvp.so"))
    return SDL_SetError("Plant OS Vulkan uses the linked lavapipe driver");
  SDL_FunctionPointer enumerate =
      (SDL_FunctionPointer)vk_icdGetInstanceProcAddr(
          VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
  if (!enumerate)
    return SDL_SetError("Lavapipe has no instance extension entry point");
  device->vulkan_config.vkGetInstanceProcAddr =
      (SDL_FunctionPointer)vk_icdGetInstanceProcAddr;
  device->vulkan_config.vkEnumerateInstanceExtensionProperties = enumerate;
  /* The ELF interpreter owns the linked ICD for the process lifetime. */
  device->vulkan_config.loader_handle = (SDL_SharedObject *)device;
  SDL_strlcpy(device->vulkan_config.loader_path, path ? path : "liblvp.so",
              sizeof(device->vulkan_config.loader_path));
  return true;
}

static void PLOS_Vulkan_UnloadLibrary(SDL_VideoDevice *device) {
  device->vulkan_config.loader_handle = NULL;
  device->vulkan_config.vkGetInstanceProcAddr = NULL;
  device->vulkan_config.vkEnumerateInstanceExtensionProperties = NULL;
}

static const char *const *
PLOS_Vulkan_GetInstanceExtensions(SDL_VideoDevice *device, Uint32 *count) {
  static const char *const extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME};
  *count = SDL_arraysize(extensions);
  return extensions;
}

static bool PLOS_Vulkan_CreateSurface(SDL_VideoDevice *device,
                                      SDL_Window *window, VkInstance instance,
                                      const VkAllocationCallbacks *allocator,
                                      VkSurfaceKHR *surface) {
  SDL_WindowData *data = window->internal;
  VkRect2D client = {.offset = {BORDER, TITLE},
                     .extent = {data->width, data->height}};
  VkResult result = plant_vulkan_create_surface(instance, data->handle, &client,
                                                allocator, surface);
  return result == VK_SUCCESS ||
         SDL_SetError("Cannot create the Vulkan window surface: %s",
                      SDL_Vulkan_GetResultString(result));
}

static void PLOS_Vulkan_DestroySurface(SDL_VideoDevice *device,
                                       VkInstance instance,
                                       VkSurfaceKHR surface,
                                       const VkAllocationCallbacks *allocator) {
  SDL_Vulkan_DestroySurface_Internal(
      device->vulkan_config.vkGetInstanceProcAddr, instance, surface,
      allocator);
}

static bool PLOS_Vulkan_GetPresentationSupport(SDL_VideoDevice *device,
                                               VkInstance instance,
                                               VkPhysicalDevice physical,
                                               Uint32 family) {
  PFN_vkGetPhysicalDeviceProperties properties =
      (PFN_vkGetPhysicalDeviceProperties)vk_icdGetInstanceProcAddr(
          instance, "vkGetPhysicalDeviceProperties");
  PFN_vkGetPhysicalDeviceQueueFamilyProperties queues =
      (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vk_icdGetInstanceProcAddr(
          instance, "vkGetPhysicalDeviceQueueFamilyProperties");
  if (!properties || !queues)
    return false;
  VkPhysicalDeviceProperties info;
  uint32_t count = 0;
  properties(physical, &info);
  queues(physical, &count, NULL);
  return info.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && family < count;
}
#endif

#ifdef SDL_VIDEO_OPENGL_EGL
static bool PLOS_GL_LoadLibrary(SDL_VideoDevice *device, const char *path) {
  if (path && SDL_strcmp(path, "libGL.so") && SDL_strcmp(path, "/lib/libGL.so"))
    return SDL_SetError("Plant OS provides the native Mesa OpenGL library");
  if (device->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_ES)
    return SDL_SetError("OpenGL ES is not enabled");
  if (!SDL_EGL_LoadLibrary(device, NULL, EGL_DEFAULT_DISPLAY, 0))
    return false;
  SDL_strlcpy(device->gl_config.driver_path, path ? path : "libGL.so",
              sizeof(device->gl_config.driver_path));
  return true;
}

static bool PLOS_GL_SetSwapInterval(SDL_VideoDevice *device, int interval) {
  if (interval != 0)
    return SDL_SetError("Plant OS OpenGL supports swap interval 0");
  return SDL_EGL_SetSwapInterval(device, interval);
}

SDL_EGL_CreateContext_impl(PLOS) SDL_EGL_MakeCurrent_impl(PLOS)
    SDL_EGL_SwapWindow_impl(PLOS)
#endif

        static SDL_VideoDevice *PLOS_CreateDevice(void) {
  SDL_VideoDevice *device = SDL_calloc(1, sizeof(*device));
  if (!device) {
    SDL_OutOfMemory();
    return NULL;
  }
  device->VideoInit = PLOS_VideoInit;
  device->VideoQuit = PLOS_VideoQuit;
  device->GetDisplayModes = PLOS_GetDisplayModes;
  device->SetDisplayMode = PLOS_SetDisplayMode;
  device->CreateSDLWindow = PLOS_CreateWindow;
  device->CreateWindowFramebuffer = PLOS_CreateWindowFramebuffer;
  device->UpdateWindowFramebuffer = PLOS_UpdateWindowFramebuffer;
  device->SetWindowTitle = PLOS_SetWindowTitle;
  device->SetWindowSize = PLOS_SetWindowSize;
  device->SetWindowResizable = PLOS_SetWindowResizable;
  device->SetWindowMouseGrab = PLOS_SetWindowMouseGrab;
  device->DestroyWindow = PLOS_DestroyWindow;
  device->PumpEvents = PLOS_PumpEvents;
#ifdef SDL_VIDEO_OPENGL_EGL
  device->GL_LoadLibrary = PLOS_GL_LoadLibrary;
  device->GL_UnloadLibrary = SDL_EGL_UnloadLibrary;
  device->GL_GetProcAddress = SDL_EGL_GetProcAddressInternal;
  device->GL_CreateContext = PLOS_GLES_CreateContext;
  device->GL_MakeCurrent = PLOS_GLES_MakeCurrent;
  device->GL_SetSwapInterval = PLOS_GL_SetSwapInterval;
  device->GL_GetSwapInterval = SDL_EGL_GetSwapInterval;
  device->GL_SwapWindow = PLOS_GLES_SwapWindow;
  device->GL_DestroyContext = SDL_EGL_DestroyContext;
#endif
#ifdef SDL_VIDEO_VULKAN
  device->Vulkan_LoadLibrary = PLOS_Vulkan_LoadLibrary;
  device->Vulkan_UnloadLibrary = PLOS_Vulkan_UnloadLibrary;
  device->Vulkan_GetInstanceExtensions = PLOS_Vulkan_GetInstanceExtensions;
  device->Vulkan_CreateSurface = PLOS_Vulkan_CreateSurface;
  device->Vulkan_DestroySurface = PLOS_Vulkan_DestroySurface;
  device->Vulkan_GetPresentationSupport = PLOS_Vulkan_GetPresentationSupport;
#endif
  device->free = PLOS_DeleteDevice;
  return device;
}

VideoBootStrap PLOS_bootstrap = {"plos", "Plant OS GUI", PLOS_CreateDevice,
                                 NULL, false};
