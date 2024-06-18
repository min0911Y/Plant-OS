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
#include "../../SDL_internal.h"
#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
#include "SDL_assert.h"
#include "SDL_plosdata.h"
#include <stdio.h>

int PLOS_GetCurrentVideoMode(SDL_DisplayMode *mode) {
  mode->w = 1024;
  mode->h = 768;
  mode->refresh_rate = 0;
  mode->format = SDL_PIXELFORMAT_ARGB8888;
  mode->driverdata = NULL;
  // printf("BOOKOS_GetCurrentVideoMode: %dx%d@%d.%x\n", mode->w, mode->h,
  // mode->refresh_rate, mode->format);
  return 0;
}

/**
 * Initializes the BOOKOS video plugin.
 * Creates the Screen context and event handles used for all window operations
 * by the plugin.
 * @param   _THIS
 * @return  0 if successful, -1 on error
 */
static int PLOS_VideoInit(_THIS) {
  SDL_VideoDisplay display;
  SDL_DisplayMode current_mode;
  PLOS_GetCurrentVideoMode(&current_mode);

  SDL_zero(display);
  display.desktop_mode = current_mode;
  display.current_mode = current_mode;
  display.driverdata = NULL;
  if (SDL_AddVideoDisplay(&display) < 0) {
    return -1;
  }

  _this->num_displays = 1;
  return 0;
}

static void PLOS_VideoQuit(_THIS) {}

/**
 * Creates a new native Screen window and associates it with the given SDL
 * window.
 * @param   _THIS
 * @param   window  SDL window to initialize
 * @return  0 if successful, -1 on error
 */
static int PLOS_CreateWindow(_THIS, SDL_Window *window) {
  SDL_WindowData *data;
  SDL_VideoData *data1 = (SDL_VideoData *)_this->driverdata;
  if (window->flags & SDL_WINDOW_OPENGL) {
    printf("plantos do not support OPENGL!\n");
    return -1;
  }

  data = SDL_calloc(1, sizeof(*data));
  if (data == NULL) {
    return -1;
  }

  data->window =
      create_window("SDL2_Window", 0, 0, window->w + 2, window->h + 21);
  data->deviceData = (SDL_VideoData *)_this->driverdata; /* 指向设备数据 */
  window->driverdata = data; /* SDL_Window->driverdata -> SDL_WindowData */
  data1->wnd = data->window;
  data1->window = window;
  data1->shift = 0;
  data1->caps_lock = 0;
  data1->ctrl = 0;
  data1->alt = 0;
  window_start_recv_keyboard(data->window);
  return 0;
}

/**
 * Gets a pointer to the Screen buffer associated with the given window. Note
 * that the buffer is actually created in createWindow().
 * @param       _THIS
 * @param       window  SDL window to get the buffer for
 * @param[out]  pixles  Holds a pointer to the window's buffer
 * @param[out]  format  Holds the pixel format for the buffer
 * @param[out]  pitch   Holds the number of bytes per line
 * @return  0 if successful, -1 on error
 */
static int PLOS_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                        Uint32 *format, void **pixels,
                                        int *pitch) {
  SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
  data->fb1 = malloc(window->w * window->h * 4);
  data->fb = window_get_fb(data->window);
  *pixels = data->fb1;
  *format = SDL_PIXELFORMAT_ARGB8888;
  /* Calculate pitch */
  *pitch = window->w * 4;
  return 0;
}

/**
 * Informs the window manager that the window needs to be updated.
 * @param   _THIS
 * @param   window      The window to update
 * @param   rects       An array of reectangular areas to update
 * @param   numrects    Rect array length
 * @return  0 if successful, -1 on error
 */
static int PLOS_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                        const SDL_Rect *rects, int numrects) {
  SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
  int x1 = rects->x, y1 = rects->y, x2 = 0, y2 = 0;
  while (--numrects >= 0) {
    x1 = SDL_min(x1, rects->x);
    y1 = SDL_min(y1, rects->y);
    x2 = SDL_max(x2, rects->x + rects->w);
    y2 = SDL_max(y2, rects->y + rects->h);
    rects++;
  }
  unsigned x1y1, x2y2;
  x1y1 = ((x1 + 2) << 16) | ((y1 + 21) & 0xffff);
  x2y2 = ((x2 + 2) << 16) | ((y2 + 21) & 0xffff);
  for (int i = x1; i < x2; i++) {
    for (int j = y1; j < y2; j++) {
      data->fb[(j + 21) * (window->w + 2) + (i + 2)] =
          data->fb1[j * window->w + i];
    }
  }
  window_refresh(data->window, x1y1, x2y2);
  return 0;
}
void PLOS_DestroyWindowFramebuffer(_THIS, SDL_Window *window) {
  SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
  free(data->fb1);
}
/**
 * Updates the size of the native window using the geometry of the SDL window.
 * @param   _THIS
 * @param   window  SDL window to update
 */
static void PLOS_SetWindowSize(_THIS, SDL_Window *window) {}

/**
 * Makes the native window associated with the given SDL window visible.
 * @param   _THIS
 * @param   window  SDL window to update
 */
static void PLOS_ShowWindow(_THIS, SDL_Window *window) {}

/**
 * Makes the native window associated with the given SDL window invisible.
 * @param   _THIS
 * @param   window  SDL window to update
 */
static void PLOS_HideWindow(_THIS, SDL_Window *window) {}

/**
 * Destroys the native window associated with the given SDL window.
 * @param   _THIS
 * @param   window  SDL window that is being destroyed
 */
static void PLOS_DestroyWindow(_THIS, SDL_Window *window) {
  SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

  if (data) {
    window_stop_recv_keyboard(data->window);
    close_window(data->window);
    if (window->driverdata) {
      SDL_free(window->driverdata);
    }
    window->driverdata = NULL;
  }
}

static void PLOS_SetWindowTitle(_THIS, SDL_Window *window) {}

static void PLOS_SetWindowPosition(_THIS, SDL_Window *window) {}

static void PLOS_SetWindowResizable(_THIS, SDL_Window *window,
                                    SDL_bool resizable) {}

static void PLOS_MaximizeWindow(_THIS, SDL_Window *window) {}

static void PLOS_MinimizeWindow(_THIS, SDL_Window *window) {}

static void PLOS_RestoreWindow(_THIS, SDL_Window *window) {}

static void PLOS_SetWindowFullscreen(_THIS, SDL_Window *window,
                                     SDL_VideoDisplay *display,
                                     SDL_bool fullscreen) {}

static void PLOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display) {
  /* FIXME: get modes from hardware, only one modes now. */
  SDL_DisplayMode mode;
  if (!PLOS_GetCurrentVideoMode(&mode)) {
    SDL_AddDisplayMode(display, &mode);
  }
}

static int PLOS_SetDisplayMode(_THIS, SDL_VideoDisplay *display,
                               SDL_DisplayMode *mode) {
  // printf("BOOKOS_SetDisplayMode: %dx%d@%d.%x\n", mode->w, mode->h,
  // mode->refresh_rate, mode->format);

  /* do not support now! */
  return 0;
}

/**
 * 开始接收文本输入，按键产生文本字符串
 */
static void PLOS_StartTextInput(_THIS) {

  SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
  data->startTextInput = 1;
}

/**
 * 结束文本输入，按键不产生文本字符串，是默认的按键值
 */
static void PLOS_StopTextInput(_THIS) {
  SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
  data->startTextInput = 0;
}

/**
 * 设置输入法已经输入的文本的区域
 */
static void PLOS_SetTextInputRect(_THIS, SDL_Rect *rect) {}

/**
 * Frees the plugin object created by createDevice().
 * @param   device  Plugin object to free
 */
static void PLOS_DeleteDevice(SDL_VideoDevice *device) { SDL_free(device); }

/**
 * Creates the BOOKOS video plugin used by SDL.
 * @param   devindex    Unused
 * @return  Initialized device if successful, NULL otherwise
 */
enum {
  MOUSE_STAY = 1,
  MOUSE_CLICK_LEFT,
  MOUSE_CLICK_RIGHT,
  CLOSE_WINDOW,
  MOUSE_WHEEL
};
static int f = 0;
char keytable[0x54] = { // 按下Shift
    0,    0x01, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    10,   0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  'D', '8', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
char keytable1[0x54] = { // 未按下Shift
    0,    0x01, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    10,   0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
uint32_t to_sdl_code(uint32_t code) {
  switch (code) {
  case 0x1e:
    return SDL_SCANCODE_A;
  case 0x30:
    return SDL_SCANCODE_B;
  case 0x2e:
    return SDL_SCANCODE_C;
  case 0x20:
    return SDL_SCANCODE_D;
  case 0x12:
    return SDL_SCANCODE_E;
  case 0x21:
    return SDL_SCANCODE_F;
  case 0x22:
    return SDL_SCANCODE_G;
  case 0x23:
    return SDL_SCANCODE_H;
  case 0x17:
    return SDL_SCANCODE_I;
  case 0x24:
    return SDL_SCANCODE_J;
  case 0x25:
    return SDL_SCANCODE_K;
  case 0x26:
    return SDL_SCANCODE_L;
  case 0x32:
    return SDL_SCANCODE_M;
  case 0x31:
    return SDL_SCANCODE_N;
  case 0x18:
    return SDL_SCANCODE_O;
  case 0x19:
    return SDL_SCANCODE_P;
  case 0x10:
    return SDL_SCANCODE_Q;
  case 0x13:
    return SDL_SCANCODE_R;
  case 0x1f:
    return SDL_SCANCODE_S;
  case 0x14:
    return SDL_SCANCODE_T;
  case 0x16:
    return SDL_SCANCODE_U;
  case 0x2f:
    return SDL_SCANCODE_V;
  case 0x11:
    return SDL_SCANCODE_W;
  case 0x2d:
    return SDL_SCANCODE_X;
  case 0x15:
    return SDL_SCANCODE_Y;
  case 0x2c:
    return SDL_SCANCODE_Z;
  case 0x02:
    return SDL_SCANCODE_1;
  case 0x03:
    return SDL_SCANCODE_2;
  case 0x04:
    return SDL_SCANCODE_3;
  case 0x05:
    return SDL_SCANCODE_4;
  case 0x06:
    return SDL_SCANCODE_5;
  case 0x07:
    return SDL_SCANCODE_6;
  case 0x08:
    return SDL_SCANCODE_7;
  case 0x09:
    return SDL_SCANCODE_8;
  case 0x0a:
    return SDL_SCANCODE_9;
  case 0x0b:
    return SDL_SCANCODE_0;
  case 0x1c:
    return SDL_SCANCODE_RETURN;
  case 0x01:
    return SDL_SCANCODE_ESCAPE;
  case 0x0e:
    return SDL_SCANCODE_BACKSPACE;
  case 0x0f:
    return SDL_SCANCODE_TAB;
  case 0x39:
    return SDL_SCANCODE_SPACE;
  case 0x0c:
    return SDL_SCANCODE_MINUS;
  case 0x0d:
    return SDL_SCANCODE_EQUALS;
  case 0x1a:
    return SDL_SCANCODE_LEFTBRACKET;
  case 0x1b:
    return SDL_SCANCODE_RIGHTBRACKET;
  case 0x2b:
    return SDL_SCANCODE_BACKSLASH;
  case 0x27:
    return SDL_SCANCODE_SEMICOLON;
  case 0x28:
    return SDL_SCANCODE_APOSTROPHE;
  case 0x29:
    return SDL_SCANCODE_GRAVE;
  case 0x33:
    return SDL_SCANCODE_COMMA;
  case 0x34:
    return SDL_SCANCODE_PERIOD;
  case 0x35:
    return SDL_SCANCODE_SLASH;
  case 0x3a:
    return SDL_SCANCODE_CAPSLOCK;
  case 0x3b:
    return SDL_SCANCODE_F1;
  case 0x3c:
    return SDL_SCANCODE_F2;
  case 0x3d:
    return SDL_SCANCODE_F3;
  case 0x3e:
    return SDL_SCANCODE_F4;
  case 0x3f:
    return SDL_SCANCODE_F5;
  case 0x40:
    return SDL_SCANCODE_F6;
  case 0x41:
    return SDL_SCANCODE_F7;
  case 0x42:
    return SDL_SCANCODE_F8;
  case 0x43:
    return SDL_SCANCODE_F9;
  case 0x44:
    return SDL_SCANCODE_F10;
  case 0x57:
    return SDL_SCANCODE_F11;
  case 0x58:
    return SDL_SCANCODE_F12;
  case 0xd2:
    return SDL_SCANCODE_INSERT;
  case 0xc7:
    return SDL_SCANCODE_HOME;
  case 0xc9:
    return SDL_SCANCODE_PAGEUP;
  case 0xd3:
    return SDL_SCANCODE_DELETE;
  case 0xd1:
    return SDL_SCANCODE_PAGEDOWN;
  case 0xcd:
    return SDL_SCANCODE_RIGHT;
  case 0xcb:
    return SDL_SCANCODE_LEFT;
  case 0xd0:
    return SDL_SCANCODE_DOWN;
  case 0xc8:
    return SDL_SCANCODE_UP;
  case 0x45:
    return SDL_SCANCODE_NUMLOCKCLEAR;
  case 0xb5:
    return SDL_SCANCODE_KP_DIVIDE;
  case 0x37:
    return SDL_SCANCODE_KP_MULTIPLY;
  case 0x4a:
    return SDL_SCANCODE_KP_MINUS;
  case 0x4e:
    return SDL_SCANCODE_KP_PLUS;
  case 0x9c:
    return SDL_SCANCODE_KP_ENTER;
  case 0x4f:
    return SDL_SCANCODE_KP_1;
  case 0x50:
    return SDL_SCANCODE_KP_2;
  case 0x51:
    return SDL_SCANCODE_KP_3;
  case 0x4b:
    return SDL_SCANCODE_KP_4;
  case 0x4c:
    return SDL_SCANCODE_KP_5;
  case 0x4d:
    return SDL_SCANCODE_KP_6;
  case 0x47:
    return SDL_SCANCODE_KP_7;
  case 0x48:
    return SDL_SCANCODE_KP_8;
  case 0x49:
    return SDL_SCANCODE_KP_9;
  case 0x52:
    return SDL_SCANCODE_KP_0;
  case 0x53:
    return SDL_SCANCODE_KP_PERIOD;

  case 0x1d:
    return SDL_SCANCODE_LCTRL;
  case 0x2a:
    return SDL_SCANCODE_LSHIFT;
  case 0x38:
    return SDL_SCANCODE_LALT;
  /* SDL_SCANCODE_LGUI */
  case 0x9d:
    return SDL_SCANCODE_RCTRL;
  case 0x38 + 0x80:
    return SDL_SCANCODE_RALT;
  default:
    break;
  }
  return SDL_SCANCODE_UNKNOWN;
}
SDL_bool SDL_SacncodeVisiable(uint32_t scancode) {
  if ((scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_0) ||
      (scancode == SDL_SCANCODE_SPACE) ||
      (scancode >= SDL_SCANCODE_MINUS && scancode <= SDL_SCANCODE_SLASH) ||
      (scancode >= SDL_SCANCODE_KP_DIVIDE &&
       scancode <= SDL_SCANCODE_KP_PERIOD))
    return SDL_TRUE;
  return SDL_FALSE;
}
void PLOS_PumpEvents(_THIS) {
  SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;

  if (data->wnd) {
    if (window_get_key_press_status(data->wnd)) {
      SDL_SetKeyboardFocus(data->window);
      int i = window_get_key_press_data(data->wnd);
      if (i == 0xe0) {
        while (!window_get_key_press_status(data->wnd))
          ;
        i = window_get_key_press_data(data->wnd);
        i += 0x80;
      }
      uint32_t scancode = to_sdl_code(i);
      if (scancode == SDL_SCANCODE_LSHIFT) {
        data->shift = 1;
      }
      if (scancode == SDL_SCANCODE_CAPSLOCK) {
        data->caps_lock = !data->caps_lock;
      }
      if (scancode == SDL_SCANCODE_LCTRL) {
        data->ctrl = 1;
      }
      if (scancode == SDL_SCANCODE_LALT) {
        data->alt = 1;
      }
      if (SDL_SacncodeVisiable(scancode) && data->startTextInput &&
          data->ctrl == 0 && data->alt == 0) {
        char s[2] = {0};
        if (data->shift || data->caps_lock) {
          s[0] = keytable[i];
        } else {
          s[0] = keytable1[i];
        }
        if (s[0]) {
          SDL_SendKeyboardText(s);
        }
      }
      SDL_SendKeyboardKey(SDL_PRESSED, scancode);
    }
    if (window_get_key_up_status(data->wnd)) {
      SDL_SetKeyboardFocus(data->window);
      int i = window_get_key_up_data(data->wnd);
      if (i == 0xe0) {
        while (!window_get_key_up_status(data->wnd))
          ;
        i = window_get_key_up_data(data->wnd);
      } else {
        i -= 0x80;
      }
      uint32_t scancode = to_sdl_code(i);
      if (scancode == SDL_SCANCODE_LSHIFT) {
        data->shift = 0;
      }
      if (scancode == SDL_SCANCODE_LCTRL) {
        data->ctrl = 0;
      }
      if (scancode == SDL_SCANCODE_LALT) {
        data->alt = 0;
      }
      SDL_SendKeyboardKey(SDL_RELEASED, scancode);
    }
    int i = window_get_event(data->wnd);
    if (i == -1)
      return;
    if (i == MOUSE_CLICK_LEFT) {
      f = 1;
      int j = window_get_event(data->wnd);
      SDL_SendMouseMotion(data->window, 0, 0, (j >> 16) - 2, (j & 0xffff) - 21);
      SDL_SendMouseButton(data->window, 0, SDL_PRESSED, SDL_BUTTON_LEFT);
    }
    if (i == MOUSE_CLICK_RIGHT) {
      f = 2;
      int j = window_get_event(data->wnd);
      SDL_SendMouseMotion(data->window, 0, 0, (j >> 16) - 2, (j & 0xffff) - 21);
      SDL_SendMouseButton(data->window, 0, SDL_PRESSED, SDL_BUTTON_RIGHT);
    }
    if (i == MOUSE_STAY) {
      if (f) {
        int btn;
        if (f == 1)
          btn = SDL_BUTTON_LEFT;
        if (f == 2)
          btn = SDL_BUTTON_RIGHT;
        int j = window_get_event(data->wnd);
        SDL_SendMouseMotion(data->window, 0, 0, (j >> 16) - 2,
                            (j & 0xffff) - 21);
        SDL_SendMouseButton(data->window, 0, SDL_RELEASED, btn);

        f = 0;
      } else {
        int j = window_get_event(data->wnd);
        SDL_SendMouseMotion(data->window, 0, 0, (j >> 16) - 2,
                            (j & 0xffff) - 21);
      }
    }
    if (i == MOUSE_WHEEL) {
      int j = window_get_event(data->wnd);
      SDL_SendMouseMotion(data->window, 0, 0, (j >> 16) - 2, (j & 0xffff) - 21);
      int k = window_get_event(data->wnd);
      float x = 0.0f, y = 0.0f;
      y = (k == 1) ? 0 + 1.0f : 0 - 1.0f;
      SDL_SendMouseWheel(data->window, 0, x, y, SDL_MOUSEWHEEL_NORMAL);
    }
    if (i == CLOSE_WINDOW) {
      SDL_SendWindowEvent(data->window, SDL_WINDOWEVENT_CLOSE, 0, 0);
    }
  }
}
SDL_bool PLOS_HasScreenKeyboardSupport(_THIS) { return SDL_FALSE; }
static SDL_VideoDevice *PLOS_CreateDevice(int devindex) {
  SDL_VideoDevice *device;

  device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
  if (device == NULL) {
    return NULL;
  }

  SDL_VideoData *data;
  data = (SDL_VideoData *)SDL_calloc(1, sizeof(SDL_VideoData));
  if (data == NULL) {
    SDL_free(device);
    return NULL;
  }
  data->startTextInput = 0; /* 默认没打开 */
  device->driverdata = data;
  device->VideoInit = PLOS_VideoInit;
  device->VideoQuit = PLOS_VideoQuit;
  device->SetDisplayMode = PLOS_SetDisplayMode;
  device->GetDisplayModes = PLOS_GetDisplayModes;
  device->CreateSDLWindow = PLOS_CreateWindow;
  device->CreateWindowFramebuffer = PLOS_CreateWindowFramebuffer;
  device->UpdateWindowFramebuffer = PLOS_UpdateWindowFramebuffer;
  device->SetWindowTitle = PLOS_SetWindowTitle;
  device->SetWindowSize = PLOS_SetWindowSize;
  device->SetWindowPosition = PLOS_SetWindowPosition;
  device->SetWindowResizable = PLOS_SetWindowResizable;
  device->MaximizeWindow = PLOS_MaximizeWindow;
  device->MinimizeWindow = PLOS_MinimizeWindow;
  device->RestoreWindow = PLOS_RestoreWindow;
  device->SetWindowFullscreen = PLOS_SetWindowFullscreen;
  device->ShowWindow = PLOS_ShowWindow;
  device->HideWindow = PLOS_HideWindow;
  device->PumpEvents = PLOS_PumpEvents;
  device->DestroyWindow = PLOS_DestroyWindow;
  device->DestroyWindowFramebuffer = PLOS_DestroyWindowFramebuffer;
  /* TODO:
  device->SetClipboardText;
  device->GetClipboardText;
  device->HasClipboardText;
  */
  device->StartTextInput = PLOS_StartTextInput;
  device->StopTextInput = PLOS_StopTextInput;
  device->SetTextInputRect = PLOS_SetTextInputRect;
  device->HasScreenKeyboardSupport = PLOS_HasScreenKeyboardSupport;
  device->free = PLOS_DeleteDevice;
  return device;
}

static int PlantOS_Available() { return 1; }

VideoBootStrap PLOS_bootstrap = {"Plant OS", "PlantOS Screen",
                                 PlantOS_Available, PLOS_CreateDevice};
