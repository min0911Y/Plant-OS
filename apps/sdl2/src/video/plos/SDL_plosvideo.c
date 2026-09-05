/*
  Sdatae DirectMedia Layer
  Copyright (C) 2017 BlackBerry Limited

  This software is provided 'as-is', without any express or dataied
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

/* GUI window decorations surround the SDL client area. */
enum { BORDER = 4, TITLE = 24 };

typedef struct {
  window_t handle;
  int width, height;
  Uint8 prefix[2];
  Uint8 button;
} SDL_WindowData;

static int PLOS_VideoInit(_THIS) {
  framebuffer_info_t info;
  if (framebuffer_info(&info) < 0)
    return SDL_SetError("Plant OS display is unavailable");
  SDL_DisplayMode mode = {SDL_PIXELFORMAT_ARGB8888, info.width, info.height, 0, NULL};
  return SDL_AddBasicVideoDisplay(&mode);
}

static void PLOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display) {
  SDL_AddDisplayMode(display, &display->desktop_mode);
}

static int PLOS_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode) {
  if (mode->w != display->desktop_mode.w || mode->h != display->desktop_mode.h ||
      mode->format != display->desktop_mode.format)
    return SDL_SetError("Plant OS uses a fixed desktop mode");
  return 0;
}

static int PLOS_CreateWindow(_THIS, SDL_Window *window) {
  if (window->flags & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN))
    return SDL_SetError("Plant OS supports software rendering");
  SDL_WindowData *data = SDL_calloc(1, sizeof(*data));
  if (!data)
    return SDL_OutOfMemory();
  data->width = window->w;
  data->height = window->h;
  data->handle = create_window(window->title ? window->title : "SDL2", window->x,
                              window->y, window->w + 2 * BORDER,
                              window->h + TITLE + BORDER);
  if (!data->handle) {
    SDL_free(data);
    return SDL_SetError("Cannot create a GUI window; start gui.bin first");
  }
  window->driverdata = data;
  window_start_recv_keyboard(data->handle);
  SDL_SetMouseFocus(window);
  SDL_SetKeyboardFocus(window);
  return 0;
}

static int PLOS_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                        Uint32 *format, void **pixels, int *pitch) {
  SDL_WindowData *data = window->driverdata;
  Uint32 *framebuffer = window_get_fb(data->handle);
  if (!framebuffer)
    return SDL_SetError("GUI framebuffer mapping is unavailable");
  int stride = data->width + 2 * BORDER;
  *pixels = framebuffer + TITLE * stride + BORDER;
  *pitch = stride * sizeof(*framebuffer);
  *format = SDL_PIXELFORMAT_ARGB8888;
  return 0;
}

static int PLOS_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                        const SDL_Rect *rects, int count) {
  SDL_WindowData *data = window->driverdata;
  SDL_Rect bounds = {0, 0, data->width, data->height};
  SDL_Rect damage = {0};
  for (int i = 0; i < count; i++) {
    SDL_Rect clipped;
    if (SDL_IntersectRect(&bounds, &rects[i], &clipped))
      SDL_UnionRect(&damage, &clipped, &damage);
  }
  if (!SDL_RectEmpty(&damage)) {
    unsigned x = damage.x + BORDER, y = damage.y + TITLE;
    if (window_present(data->handle, (x << 16) | y,
                        ((x + damage.w) << 16) | (y + damage.h)) != 0)
      return SDL_SetError("Cannot present the GUI window framebuffer");
  }
  return 0;
}

static void PLOS_SetWindowTitle(_THIS, SDL_Window *window) {
  SDL_WindowData *data = window->driverdata;
  if (window_set_title(data->handle, window->title ? window->title : "") != 0)
    SDL_SetError("Cannot update the GUI window title");
}

static void PLOS_SetWindowSize(_THIS, SDL_Window *window) {
  /* The GUI protocol currently exposes fixed-size windows. */
  SDL_WindowData *data = window->driverdata;
  window->w = data->width;
  window->h = data->height;
}

static void PLOS_DestroyWindow(_THIS, SDL_Window *window) {
  SDL_WindowData *data = window->driverdata;
  if (!data)
    return;
  window_stop_recv_keyboard(data->handle);
  close_window(data->handle);
  SDL_free(data);
  window->driverdata = NULL;
}

static const char keytable[0x54] = { // 按下Shift
    0,    0x01, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    10,   0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  'D', '8', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
static const char keytable1[0x54] = { // 未按下Shift
    0,    0x01, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    10,   0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
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

static void PLOS_PumpEvents(_THIS) {
  for (SDL_Window *window = _this->windows; window; window = window->next) {
    SDL_WindowData *data = window->driverdata;
    if (!data)
      continue;
    for (int released = 0; released < 2; released++) {
      int raw;
      while ((raw = released ? window_get_key_up_data(data->handle) :
                               window_get_key_press_data(data->handle)) >= 0) {
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
        SDL_SendKeyboardKey(released ? SDL_RELEASED : SDL_PRESSED, key);
        SDL_Keymod modifiers = SDL_GetModState();
        if (released || !SDL_IsTextInputActive() ||
            (modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) || code >= sizeof(keytable))
          continue;
        char text[2] = {keytable1[code], 0};
        int shifted = (modifiers & KMOD_SHIFT) != 0;
        if (text[0] >= 'a' && text[0] <= 'z')
          shifted ^= (modifiers & KMOD_CAPS) != 0;
        if (shifted)
          text[0] = keytable[code];
        if (text[0] >= 32 && text[0] < 127)
          SDL_SendKeyboardText(text);
      }
    }
    int event;
    while ((event = window_get_event(data->handle)) >= 0) {
      if (event == GUI_EVENT_CLOSE_WINDOW) {
        SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
        continue;
      }
      unsigned position = window_get_event(data->handle);
      SDL_SendMouseMotion(window, 0, 0, (position >> 16) - BORDER,
                           (position & 0xffff) - TITLE);
      if (event == GUI_EVENT_MOUSE_WHEEL) {
        int direction = window_get_event(data->handle);
        SDL_SendMouseWheel(window, 0, 0, direction == 1 ? 1 : -1, SDL_MOUSEWHEEL_NORMAL);
        continue;
      }
      Uint8 button = event == GUI_EVENT_MOUSE_CLICK_LEFT ? SDL_BUTTON_LEFT :
                     event == GUI_EVENT_MOUSE_CLICK_RIGHT ? SDL_BUTTON_RIGHT : 0;
      if (button == data->button)
        continue;
      if (data->button)
        SDL_SendMouseButton(window, 0, SDL_RELEASED, data->button);
      if (button)
        SDL_SendMouseButton(window, 0, SDL_PRESSED, button);
      data->button = button;
    }
  }
}

/* SDL requires this callback; window teardown owns all native resources. */
static void PLOS_VideoQuit(_THIS) {}

static void PLOS_DeleteDevice(SDL_VideoDevice *device) { SDL_free(device); }

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
  device->SetWindowSize = PLOS_SetWindowSize;
  device->SetWindowTitle = PLOS_SetWindowTitle;
  device->DestroyWindow = PLOS_DestroyWindow;
  device->PumpEvents = PLOS_PumpEvents;
  device->free = PLOS_DeleteDevice;
  return device;
}

VideoBootStrap PLOS_bootstrap = {"Plant OS", "Plant OS GUI", PLOS_CreateDevice, NULL};
