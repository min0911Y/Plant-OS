// SPDX-License-Identifier: Zlib
#include "internal.h"
#include <string.h>

// GUI uses the shared x86 logical scan codes for both PS/2 and USB keyboards.
static const short keycodes[256] = {
    [0x1e] = GLFW_KEY_A,
    [0x30] = GLFW_KEY_B,
    [0x2e] = GLFW_KEY_C,
    [0x20] = GLFW_KEY_D,
    [0x12] = GLFW_KEY_E,
    [0x21] = GLFW_KEY_F,
    [0x22] = GLFW_KEY_G,
    [0x23] = GLFW_KEY_H,
    [0x17] = GLFW_KEY_I,
    [0x24] = GLFW_KEY_J,
    [0x25] = GLFW_KEY_K,
    [0x26] = GLFW_KEY_L,
    [0x32] = GLFW_KEY_M,
    [0x31] = GLFW_KEY_N,
    [0x18] = GLFW_KEY_O,
    [0x19] = GLFW_KEY_P,
    [0x10] = GLFW_KEY_Q,
    [0x13] = GLFW_KEY_R,
    [0x1f] = GLFW_KEY_S,
    [0x14] = GLFW_KEY_T,
    [0x16] = GLFW_KEY_U,
    [0x2f] = GLFW_KEY_V,
    [0x11] = GLFW_KEY_W,
    [0x2d] = GLFW_KEY_X,
    [0x15] = GLFW_KEY_Y,
    [0x2c] = GLFW_KEY_Z,
    [0x02] = GLFW_KEY_1,
    [0x03] = GLFW_KEY_2,
    [0x04] = GLFW_KEY_3,
    [0x05] = GLFW_KEY_4,
    [0x06] = GLFW_KEY_5,
    [0x07] = GLFW_KEY_6,
    [0x08] = GLFW_KEY_7,
    [0x09] = GLFW_KEY_8,
    [0x0a] = GLFW_KEY_9,
    [0x0b] = GLFW_KEY_0,
    [0x1c] = GLFW_KEY_ENTER,
    [0x01] = GLFW_KEY_ESCAPE,
    [0x0e] = GLFW_KEY_BACKSPACE,
    [0x0f] = GLFW_KEY_TAB,
    [0x39] = GLFW_KEY_SPACE,
    [0x0c] = GLFW_KEY_MINUS,
    [0x0d] = GLFW_KEY_EQUAL,
    [0x1a] = GLFW_KEY_LEFT_BRACKET,
    [0x1b] = GLFW_KEY_RIGHT_BRACKET,
    [0x2b] = GLFW_KEY_BACKSLASH,
    [0x27] = GLFW_KEY_SEMICOLON,
    [0x28] = GLFW_KEY_APOSTROPHE,
    [0x29] = GLFW_KEY_GRAVE_ACCENT,
    [0x33] = GLFW_KEY_COMMA,
    [0x34] = GLFW_KEY_PERIOD,
    [0x35] = GLFW_KEY_SLASH,
    [0x3a] = GLFW_KEY_CAPS_LOCK,
    [0x3b] = GLFW_KEY_F1,
    [0x3c] = GLFW_KEY_F2,
    [0x3d] = GLFW_KEY_F3,
    [0x3e] = GLFW_KEY_F4,
    [0x3f] = GLFW_KEY_F5,
    [0x40] = GLFW_KEY_F6,
    [0x41] = GLFW_KEY_F7,
    [0x42] = GLFW_KEY_F8,
    [0x43] = GLFW_KEY_F9,
    [0x44] = GLFW_KEY_F10,
    [0x57] = GLFW_KEY_F11,
    [0x58] = GLFW_KEY_F12,
    [0xd2] = GLFW_KEY_INSERT,
    [0xc7] = GLFW_KEY_HOME,
    [0xc9] = GLFW_KEY_PAGE_UP,
    [0xd3] = GLFW_KEY_DELETE,
    [0xd1] = GLFW_KEY_PAGE_DOWN,
    [0xcd] = GLFW_KEY_RIGHT,
    [0xcb] = GLFW_KEY_LEFT,
    [0xd0] = GLFW_KEY_DOWN,
    [0xc8] = GLFW_KEY_UP,
    [0x45] = GLFW_KEY_NUM_LOCK,
    [0xb5] = GLFW_KEY_KP_DIVIDE,
    [0x37] = GLFW_KEY_KP_MULTIPLY,
    [0x4a] = GLFW_KEY_KP_SUBTRACT,
    [0x4e] = GLFW_KEY_KP_ADD,
    [0x9c] = GLFW_KEY_KP_ENTER,
    [0x4f] = GLFW_KEY_KP_1,
    [0x50] = GLFW_KEY_KP_2,
    [0x51] = GLFW_KEY_KP_3,
    [0x4b] = GLFW_KEY_KP_4,
    [0x4c] = GLFW_KEY_KP_5,
    [0x4d] = GLFW_KEY_KP_6,
    [0x47] = GLFW_KEY_KP_7,
    [0x48] = GLFW_KEY_KP_8,
    [0x49] = GLFW_KEY_KP_9,
    [0x52] = GLFW_KEY_KP_0,
    [0x53] = GLFW_KEY_KP_DECIMAL,
    [0x1d] = GLFW_KEY_LEFT_CONTROL,
    [0x2a] = GLFW_KEY_LEFT_SHIFT,
    [0x38] = GLFW_KEY_LEFT_ALT,
    [0x9d] = GLFW_KEY_RIGHT_CONTROL,
    [0x36] = GLFW_KEY_RIGHT_SHIFT,
    [0xcf] = GLFW_KEY_END,
    [0xb8] = GLFW_KEY_RIGHT_ALT,
    [0xdb] = GLFW_KEY_LEFT_SUPER,
    [0xdc] = GLFW_KEY_RIGHT_SUPER,
    [0xdd] = GLFW_KEY_MENU,
    [0x46] = GLFW_KEY_SCROLL_LOCK,
};
static int modifiers(_GLFWwindow *window, int key, int action) {
  static const struct {
    int first, second, mod;
  } groups[] = {
      {GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_MOD_SHIFT},
      {GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL, GLFW_MOD_CONTROL},
      {GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT, GLFW_MOD_ALT},
      {GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER, GLFW_MOD_SUPER},
  };
  int mods = window->plantos.lockMods;
  for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
    int a = groups[i].first, b = groups[i].second;
    if ((a == key ? action != GLFW_RELEASE : window->keys[a] == GLFW_PRESS) ||
        (b == key ? action != GLFW_RELEASE : window->keys[b] == GLFW_PRESS))
      mods |= groups[i].mod;
  }
  return mods;
}

static unsigned character(int key, int mods) {
  bool shift = (mods & GLFW_MOD_SHIFT) != 0;
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    return key +
           ((shift != ((mods & GLFW_MOD_CAPS_LOCK) != 0)) ? 0 : 'a' - 'A');
  if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
    return (mods & GLFW_MOD_NUM_LOCK) ? '0' + key - GLFW_KEY_KP_0 : 0;
  switch (key) {
  case GLFW_KEY_KP_ADD:
    return '+';
  case GLFW_KEY_KP_SUBTRACT:
    return '-';
  case GLFW_KEY_KP_MULTIPLY:
    return '*';
  case GLFW_KEY_KP_DIVIDE:
    return '/';
  case GLFW_KEY_KP_DECIMAL:
    return (mods & GLFW_MOD_NUM_LOCK) ? '.' : 0;
  }
  if (key < GLFW_KEY_SPACE || key > GLFW_KEY_GRAVE_ACCENT)
    return 0;
  if (shift) {
    const char *plain = "1234567890-=[]\\;',./`";
    const char *upper = "!@#$%^&*()_+{}|:\"<>?~";
    const char *match = strchr(plain, key);
    if (match)
      return upper[match - plain];
  }
  return key;
}

static void pollKeyboard(_GLFWwindow *window) {
  for (unsigned released = 0; released < 2; released++) {
    int raw;
    while ((raw = released
                      ? window_get_key_up_data(window->plantos.native.window)
                      : window_get_key_press_data(
                            window->plantos.native.window)) >= 0) {
      if (raw == 0xe0) {
        window->plantos.prefix[released] = 0x80;
        continue;
      }
      unsigned scancode = (raw & 0x7f) | window->plantos.prefix[released];
      window->plantos.prefix[released] = 0;
      int key = keycodes[scancode];
      if (!key)
        continue;
      if (!released && window->keys[key] != GLFW_PRESS) {
        if (key == GLFW_KEY_CAPS_LOCK)
          window->plantos.lockMods ^= GLFW_MOD_CAPS_LOCK;
        if (key == GLFW_KEY_NUM_LOCK)
          window->plantos.lockMods ^= GLFW_MOD_NUM_LOCK;
      }
      int action = released ? GLFW_RELEASE : GLFW_PRESS;
      int mods = modifiers(window, key, action);
      _glfwInputKey(window, key, scancode, action, mods);
      if (!released) {
        unsigned codepoint = character(key, mods);
        if (codepoint)
          _glfwInputChar(
              window, codepoint, mods,
              !(mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)));
      }
    }
  }
}

void _glfwPollEventsPlantOS(void) {
  __atomic_exchange_n(&_glfw.plantos.pending, 0, __ATOMIC_ACQ_REL);
  for (_GLFWwindow *window = _glfw.windowListHead; window;
       window = window->next) {
    if (!window->plantos.native.window)
      continue;
    pollKeyboard(window);
    int event;
    while ((event = window_get_event(window->plantos.native.window)) >= 0) {
      if (event == GUI_EVENT_CLOSE_WINDOW) {
        _glfwInputWindowCloseRequest(window);
        continue;
      }
      int position = window_get_event(window->plantos.native.window);
      if (position == -1)
        break;
      double x = (int16_t)((unsigned)position >> 16) - _GLFW_PLANT_BORDER;
      double y = (int16_t)position - _GLFW_PLANT_TITLE;
      _glfwInputCursorPos(window, x, y);
      if (event == GUI_EVENT_MOUSE_WHEEL) {
        int wheel = window_get_event(window->plantos.native.window);
        if (wheel >= 0)
          _glfwInputScroll(window, 0, wheel == 1 ? 1 : -1);
        continue;
      }
      unsigned button = event == GUI_EVENT_MOUSE_CLICK_LEFT    ? 1
                        : event == GUI_EVENT_MOUSE_CLICK_RIGHT ? 2
                                                               : 0;
      if (button != window->plantos.button) {
        int mods = modifiers(window, GLFW_KEY_UNKNOWN, GLFW_RELEASE);
        if (window->plantos.button)
          _glfwInputMouseClick(window, window->plantos.button - 1, GLFW_RELEASE,
                               mods);
        if (button)
          _glfwInputMouseClick(window, button - 1, GLFW_PRESS, mods);
        window->plantos.button = button;
      }
    }
    _glfwSyncWindowPlantOS(window);
  }
}

void _glfwGetCursorPosPlantOS(_GLFWwindow *window, double *x, double *y) {
  gui_window_state_t state;
  if (!_glfwQueryWindowPlantOS(window, &state))
    return;
  if (x)
    *x = state.cursor_x;
  if (y)
    *y = state.cursor_y;
}
void _glfwSetCursorPosPlantOS(_GLFWwindow *window, double x, double y) {
  _glfwInputUnsupportedPlantOS("Cursor warping");
}
void _glfwSetCursorModePlantOS(_GLFWwindow *window, int mode) {
  window->cursorMode = GLFW_CURSOR_NORMAL;
  if (mode != GLFW_CURSOR_NORMAL)
    _glfwInputUnsupportedPlantOS("Cursor hiding and capture");
}
void _glfwSetRawMouseMotionPlantOS(_GLFWwindow *window, GLFWbool enabled) {
  window->rawMouseMotion = GLFW_FALSE;
  if (enabled)
    _glfwInputUnsupportedPlantOS("Raw mouse motion");
}
GLFWbool _glfwRawMouseMotionSupportedPlantOS(void) { return GLFW_FALSE; }
GLFWbool _glfwCreateCursorPlantOS(_GLFWcursor *cursor, const GLFWimage *image,
                                  int x, int y) {
  _glfwInputUnsupportedPlantOS("Custom cursors");
  return GLFW_FALSE;
}
GLFWbool _glfwCreateStandardCursorPlantOS(_GLFWcursor *cursor, int shape) {
  if (shape == GLFW_ARROW_CURSOR)
    return GLFW_TRUE;
  _glfwInputUnsupportedPlantOS("Requested cursor shape");
  return GLFW_FALSE;
}
void _glfwDestroyCursorPlantOS(_GLFWcursor *cursor) {}
void _glfwSetCursorPlantOS(_GLFWwindow *window, _GLFWcursor *cursor) {}
void _glfwSetClipboardStringPlantOS(const char *string) {
  _glfwInputUnsupportedPlantOS("System clipboard");
}
const char *_glfwGetClipboardStringPlantOS(void) {
  _glfwInputUnsupportedPlantOS("System clipboard");
  return NULL;
}
int _glfwGetKeyScancodePlantOS(int key) {
  for (unsigned code = 0; code < sizeof(keycodes) / sizeof(keycodes[0]); code++)
    if (keycodes[code] == key)
      return code;
  return -1;
}
const char *_glfwGetScancodeNamePlantOS(int scancode) {
  if (scancode < 0 || scancode >= 256) {
    _glfwInputError(GLFW_INVALID_VALUE, "Plant OS: Invalid scancode");
    return NULL;
  }
  // One immutable string per printable key; previous results remain valid.
  static const char names[][2] = {
      " ", "!", "\"", "#", "$",  "%", "&", "'", "(", ")", "*", "+", ",", "-",
      ".", "/", "0",  "1", "2",  "3", "4", "5", "6", "7", "8", "9", ":", ";",
      "<", "=", ">",  "?", "@",  "A", "B", "C", "D", "E", "F", "G", "H", "I",
      "J", "K", "L",  "M", "N",  "O", "P", "Q", "R", "S", "T", "U", "V", "W",
      "X", "Y", "Z",  "[", "\\", "]", "^", "_", "`", "a", "b", "c", "d", "e",
      "f", "g", "h",  "i", "j",  "k", "l", "m", "n", "o", "p", "q", "r", "s",
      "t", "u", "v",  "w", "x",  "y", "z", "{", "|", "}", "~",
  };
  unsigned value = character(keycodes[scancode], GLFW_MOD_NUM_LOCK);
  return value >= 32 && value <= 126 ? names[value - 32] : NULL;
}
