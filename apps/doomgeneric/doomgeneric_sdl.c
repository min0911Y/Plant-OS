// // doomgeneric for cross-platform development library 'Simple DirectMedia
// Layer'

// #include "doomgeneric.h"
// #include "doomkeys.h"
// #include "m_argv.h"

// #include <stdbool.h>
// #include <stdio.h>
// #include <syscall.h>
// #include <time.h>
// #define KEYQUEUE_SIZE 16

// static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
// static unsigned int s_KeyQueueWriteIndex = 0;
// static unsigned int s_KeyQueueReadIndex = 0;
// char keytable[0x54] = { // 按下Shift
//     0,    0x01, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
//     '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
//     10,   0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
//     0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
//     0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
//     0,    '7',  'D', '8', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
// char keytable1[0x54] = { // 未按下Shift
//     0,    0x01, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
//     '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
//     10,   0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
//     0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
//     0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
//     0,    '7',  '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
// int sc2a(int sc) {
//   // 扫描码转化ASCII码
//   // 逻辑与getch函数大同小异
//   int ch = sc;
//   if (ch > 0x80) {
//     ch -= 0x80;
//     if (ch == 0x48) {
//       return -1;
//     } else if (ch == 0x50) {
//       return -2;
//     } else if (ch == 0x4b) {
//       return -3;
//     } else if (ch == 0x4d) {
//       return -4;
//     }
//   }
//   if (keytable[ch] == 0x00) {
//     return 0;
//   }
//   return keytable1[ch];
// }
// static unsigned char convertToDoomKey(unsigned int key) {
//   switch (key) {
//   case 0x1c:
//     key = KEY_ENTER;
//     break;
//   case 0x01:
//     key = KEY_ESCAPE;
//     break;
//   case 0x1e:
//     key = KEY_LEFTARROW;
//     break;
//   case 0x20:
//     key = KEY_RIGHTARROW;
//     break;
//   case 0x11:
//     key = KEY_UPARROW;
//     break;
//   case 0x1f:
//     key = KEY_DOWNARROW;
//     break;
//   case 0x1d:
//     key = KEY_FIRE;
//     break;
//   case 0x39:
//     key = KEY_USE;
//     break;
//   case 0x2a:
//   case 0x36:
//     key = KEY_RSHIFT;
//     break;
//   case 0x38:
//     key = KEY_LALT;
//     break;
//   case 0x3c:
//     key = KEY_F2;
//     break;
//   case 0x3d:
//     key = KEY_F3;
//     break;
//   case 0x3e:
//     key = KEY_F4;
//     break;
//   case 0x3f:
//     key = KEY_F5;
//     break;
//   case 0x40:
//     key = KEY_F6;
//     break;
//   case 0x41:
//     key = KEY_F7;
//     break;
//   case 0x42:
//     key = KEY_F8;
//     break;
//   case 0x43:
//     key = KEY_F9;
//     break;
//   case 0x44:
//     key = KEY_F10;
//     break;
//   case 0x57:
//     key = KEY_F11;
//     break;
//   case 0x0d:
//     key = KEY_EQUALS;
//     break;
//   case 0x0c:
//     key = KEY_MINUS;
//     break;
//   default:
//     key = tolower(sc2a(key));
//     break;
//   }

//   return key;
// }

// static void addKeyToQueue(int pressed, unsigned int keyCode) {
//   unsigned char key = convertToDoomKey(keyCode);

//   unsigned short keyData = (pressed << 8) | key;

//   s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
//   s_KeyQueueWriteIndex++;
//   s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
// }
// static void handleKeyInput() {

//   if (key_press_status()) {
//     // KeySym sym = XKeycodeToKeysym(s_Display, e.xkey.keycode, 0);
//     // printf("KeyPress:%d sym:%d\n", e.xkey.keycode, sym);
//     addKeyToQueue(1, get_key_press());
//   } else if (key_up_status()) {
//     // KeySym sym = XKeycodeToKeysym(s_Display, e.xkey.keycode, 0);
//     // printf("KeyRelease:%d sym:%d\n", e.xkey.keycode, sym);
//     addKeyToQueue(0, get_key_up()-0x80);
//   }
// }

// void DG_Init() {
//   start_keyboard_message();
//   set_mode(DOOMGENERIC_RESX,DOOMGENERIC_RESY);
// }

// void DG_DrawFrame() {
//   VBESetBuffer(0,0,DOOMGENERIC_RESX,DOOMGENERIC_RESY,DG_ScreenBuffer);
//   handleKeyInput();
// }

// void DG_SleepMs(uint32_t ms) { sleep(ms); }

// uint32_t DG_GetTicksMs() { return clock(); }

// int DG_GetKey(int *pressed, unsigned char *doomKey) {
//   if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
//     // key queue is empty
//     return 0;
//   } else {
//     unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
//     s_KeyQueueReadIndex++;
//     s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

//     *pressed = keyData >> 8;
//     *doomKey = keyData & 0xFF;

//     return 1;
//   }

//   return 0;
// }

// void DG_SetWindowTitle(const char *title) {
// }
// static void quit() {
//   SwitchToText8025_BIOS();
// }
// int main(int argc, char **argv) {
//   doomgeneric_Create(argc, argv);
//   atexit(quit);
//   for (int i = 0;; i++) {
//     doomgeneric_Tick();
//   }
//   return 0;
// }

// doomgeneric for cross-platform development library 'Simple DirectMedia Layer'

#include "doomgeneric.h"
#include "doomkeys.h"
#include "m_argv.h"

#include <stdio.h>

#include <SDL.h>
#include <stdbool.h>

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture;

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static unsigned char convertToDoomKey(unsigned int key) {
  switch (key) {
  case SDLK_RETURN:
    key = KEY_ENTER;
    break;
  case SDLK_ESCAPE:
    key = KEY_ESCAPE;
    break;
  case SDLK_LEFT:
    key = KEY_LEFTARROW;
    break;
  case SDLK_RIGHT:
    key = KEY_RIGHTARROW;
    break;
  case SDLK_UP:
    key = KEY_UPARROW;
    break;
  case SDLK_DOWN:
    key = KEY_DOWNARROW;
    break;
  case SDLK_LCTRL:
  case SDLK_RCTRL:
    key = KEY_FIRE;
    break;
  case SDLK_SPACE:
    key = KEY_USE;
    break;
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    key = KEY_RSHIFT;
    break;
  case SDLK_LALT:
  case SDLK_RALT:
    key = KEY_LALT;
    break;
  case SDLK_F2:
    key = KEY_F2;
    break;
  case SDLK_F3:
    key = KEY_F3;
    break;
  case SDLK_F4:
    key = KEY_F4;
    break;
  case SDLK_F5:
    key = KEY_F5;
    break;
  case SDLK_F6:
    key = KEY_F6;
    break;
  case SDLK_F7:
    key = KEY_F7;
    break;
  case SDLK_F8:
    key = KEY_F8;
    break;
  case SDLK_F9:
    key = KEY_F9;
    break;
  case SDLK_F10:
    key = KEY_F10;
    break;
  case SDLK_F11:
    key = KEY_F11;
    break;
  case SDLK_EQUALS:
  case SDLK_PLUS:
    key = KEY_EQUALS;
    break;
  case SDLK_MINUS:
    key = KEY_MINUS;
    break;
  default:
    key = tolower(key);
    break;
  }

  return key;
}

static void addKeyToQueue(int pressed, unsigned int keyCode) {
  unsigned char key = convertToDoomKey(keyCode);

  unsigned short keyData = (pressed << 8) | key;

  s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
  s_KeyQueueWriteIndex++;
  s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}
static void handleKeyInput() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {

      exit(1);
    }
    if (e.type == SDL_KEYDOWN) {
      // KeySym sym = XKeycodeToKeysym(s_Display, e.xkey.keycode, 0);
      // printf("KeyPress:%d sym:%d\n", e.xkey.keycode, sym);
      addKeyToQueue(1, e.key.keysym.sym);
    } else if (e.type == SDL_KEYUP) {
      // KeySym sym = XKeycodeToKeysym(s_Display, e.xkey.keycode, 0);
      // printf("KeyRelease:%d sym:%d\n", e.xkey.keycode, sym);
      addKeyToQueue(0, e.key.keysym.sym);
    }
  }
}

void DG_Init() {
  SDL_Init(SDL_INIT_VIDEO);
  window =
      SDL_CreateWindow("DOOM", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       DOOMGENERIC_RESX, DOOMGENERIC_RESY, SDL_WINDOW_SHOWN);

  // Setup renderer
  renderer = SDL_CreateRenderer(window, -1, 0);
  // Clear winow
  SDL_RenderClear(renderer);
  // Render the rect to the screen
  SDL_RenderPresent(renderer);

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
                              SDL_TEXTUREACCESS_TARGET, DOOMGENERIC_RESX,
                              DOOMGENERIC_RESY);
}

void DG_DrawFrame() {
  SDL_UpdateTexture(texture, NULL, DG_ScreenBuffer,
                    DOOMGENERIC_RESX * sizeof(uint32_t));

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);

  handleKeyInput();
}

void DG_SleepMs(uint32_t ms) { SDL_Delay(ms); }

uint32_t DG_GetTicksMs() { return SDL_GetTicks(); }

int DG_GetKey(int *pressed, unsigned char *doomKey) {
  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
    // key queue is empty
    return 0;
  } else {
    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex++;
    s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
  }

  return 0;
}

void DG_SetWindowTitle(const char *title) {
  if (window != NULL) {
    SDL_SetWindowTitle(window, title);
  }
}

int main(int argc, char **argv) {

  doomgeneric_Create(argc, argv);
  atexit(SDL_Quit);
  for (int i = 0;; i++) {
    doomgeneric_Tick();
  }

  return 0;
}