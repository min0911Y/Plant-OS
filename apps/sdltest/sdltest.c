#include <SDL.h>
#include <SDL_ttf.h>
#include <framebuffer.h>
#include <gui_rpc.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      logkf("SDLTEST FAIL line=%d error=%s\n", __LINE__, SDL_GetError());      \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int frame_checkpoint(const char *phase) {
  logkf("SDLFRAME READY %s\n", phase);
  Uint64 deadline = SDL_GetTicks64() + 10000;
  SDL_Event event;
  while (SDL_GetTicks64() < deadline) {
    if (SDL_WaitEventTimeout(&event, 100) && event.type == SDL_KEYDOWN &&
        event.key.keysym.scancode == SDL_SCANCODE_SPACE)
      return 0;
  }
  logkf("SDLFRAME FAIL timeout %s\n", phase);
  return 1;
}

static int expose_window(SDL_Window *window) {
  int x, y, width, height;
  SDL_GetWindowPosition(window, &x, &y);
  SDL_GetWindowSize(window, &width, &height);
  SDL_Window *cover = SDL_CreateWindow("Exposure test", x, y, width, height, 0);
  CHECK(cover);
  SDL_DestroyWindow(cover);
  return 0;
}

static int test_frame_publication(SDL_Window *first, SDL_Window *second,
                                   SDL_Surface *surface, SDL_Renderer *renderer) {
  /* Clearing the next frame must not change the presented image on exposure. */
  CHECK(SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255) == 0);
  CHECK(SDL_RenderClear(renderer) == 0);
  CHECK(SDL_RenderFlush(renderer) == 0);
  CHECK(expose_window(second) == 0);
  CHECK(frame_checkpoint("draft") == 0);

  /* A partial update must leave all pixels outside the damage unchanged. */
  CHECK(SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 240, 160, 32)) == 0);
  SDL_Rect damage = {0, 80, 32, 32};
  CHECK(SDL_UpdateWindowSurfaceRects(first, &damage, 1) == 0);
  CHECK(expose_window(first) == 0);
  CHECK(frame_checkpoint("partial") == 0);

  /* Return from Present is the boundary for reusing the shared render buffer. */
  CHECK(SDL_SetRenderDrawColor(renderer, 224, 48, 32, 255) == 0);
  CHECK(SDL_RenderClear(renderer) == 0);
  SDL_RenderPresent(renderer);
  CHECK(SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255) == 0);
  CHECK(SDL_RenderClear(renderer) == 0);
  CHECK(SDL_RenderFlush(renderer) == 0);
  CHECK(expose_window(second) == 0);
  CHECK(frame_checkpoint("presented") == 0);
  logkf("SDLFRAME PASS draft isolation, partial damage, present completion\n");
  return 0;
}

int main(int argc, char **argv) {
  rpc_endpoint_t gui;
  if (rpc_connect(GUI_SERVICE_NAME, &gui, 0) != RPC_OK) {
    int child = fork();
    CHECK(child >= 0);
    if (child == 0)
      return exec("gui.bin", "gui.bin");
    CHECK(rpc_connect(GUI_SERVICE_NAME, &gui, 30000) == RPC_OK);
  }
  /* The service is published before the desktop and input thread are ready. */
  sleep(1000);
  if (argc == 2) {
    logkf("SDLAPP START %s\n", argv[1]);
    int editing = strcmp(argv[1], "lite.bin") == 0;
    const char *command = argv[1];
    if (editing) {
      FILE *file = fopen("/sdltest.txt", "wb");
      CHECK(file && fwrite("Plant OS\n", 1, 9, file) == 9);
      CHECK(fclose(file) == 0);
      command = "lite.bin /sdltest.txt";
    }
    int status = exec(argv[1], (char *)command);
    if (editing && status == 0) {
      FILE *file = fopen("/sdltest.txt", "rb");
      char content[64] = {0};
      CHECK(file && fread(content, 1, sizeof(content) - 1, file) >= 10);
      fclose(file);
      CHECK(strncmp(content, "Plant OSa\n", 10) == 0);
      remove("/sdltest.txt");
      logkf("LITE EDIT PASS file input and save\n");
    }
    logkf("SDLAPP EXIT %s status=%d\n", argv[1], status);
    return status;
  }
  CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
  Uint64 start = SDL_GetPerformanceCounter();
  SDL_Delay(20);
  CHECK(SDL_GetPerformanceFrequency() == 1000000000);
  CHECK(SDL_GetPerformanceCounter() - start >= 10000000);
  CHECK(SDL_GetTicks64() >= 10);
  SDL_DisplayMode mode;
  framebuffer_info_t display;
  CHECK(framebuffer_info(&display) == 0);
  CHECK(SDL_GetCurrentDisplayMode(0, &mode) == 0);
  CHECK(mode.w == display.width && mode.h == display.height);

  SDL_Window *first = SDL_CreateWindow("SDL surface", 80, 80, 256, 160, 0);
  SDL_Window *second = SDL_CreateWindow("SDL renderer", 420, 80, 128, 128, 0);
  CHECK(first && second);
  SDL_Surface *surface = SDL_GetWindowSurface(first);
  CHECK(surface && surface->pitch >= surface->w * 4);
  CHECK(SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 24, 64, 128)) ==
        0);
  CHECK(SDL_UpdateWindowSurface(first) == 0);
  SDL_Rect clipped[] = {{-4, -4, 20, 20}, {0, 0, 0, 0}, {250, 155, 50, 50}};
  CHECK(SDL_UpdateWindowSurfaceRects(first, clipped, 3) == 0);
  CHECK(TTF_Init() == 0);
  TTF_Font *font = TTF_OpenFont("/data/fonts/mono.ttf", 18);
  CHECK(font);
  SDL_Surface *text = TTF_RenderUTF8_Blended(font, "SDL2 / SSE2",
                                             (SDL_Color){255, 255, 255, 255});
  CHECK(text);
  SDL_Rect label = {12, 16, 0, 0};
  CHECK(SDL_BlitSurface(text, NULL, surface, &label) == 0);
  SDL_FreeSurface(text);
  TTF_CloseFont(font);
  TTF_Quit();
  CHECK(SDL_UpdateWindowSurface(first) == 0);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(second, -1, SDL_RENDERER_SOFTWARE);
  CHECK(renderer);
  CHECK(SDL_SetRenderDrawColor(renderer, 32, 192, 64, 255) == 0);
  CHECK(SDL_RenderClear(renderer) == 0);
  SDL_RenderPresent(renderer);
  SDL_StartTextInput();
  logkf("SDLTEST READY origin=%u,%u target=160,160\n", display.width / 2,
        display.height / 2);
  unsigned seen = 0;
  Uint64 deadline = SDL_GetTicks64() + 20000;
  while (SDL_GetTicks64() < deadline && seen != 127) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_MOUSEMOTION && event.motion.x == 76 &&
          event.motion.y == 56)
        seen |= 1;
      if (event.type == SDL_MOUSEBUTTONDOWN &&
          event.button.button == SDL_BUTTON_LEFT)
        seen |= 2;
      if (event.type == SDL_MOUSEBUTTONUP &&
          event.button.button == SDL_BUTTON_LEFT)
        seen |= 4;
      if (event.type == SDL_MOUSEWHEEL && event.wheel.y == 1)
        seen |= 8;
      if (event.type == SDL_TEXTINPUT && strcmp(event.text.text, "a") == 0)
        seen |= 16;
      if (event.type == SDL_KEYDOWN &&
          event.key.keysym.scancode == SDL_SCANCODE_LEFT)
        seen |= 32;
      if (event.type == SDL_KEYUP &&
          event.key.keysym.scancode == SDL_SCANCODE_LEFT)
        seen |= 64;
    }
    SDL_Delay(10);
  }
  logkf("SDLTEST INPUT seen=%u\n", seen);
  CHECK(seen == 127);
  CHECK(test_frame_publication(first, second, surface, renderer) == 0);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(second);
  SDL_DestroyWindow(first);
  SDL_Quit();
  logkf("SDLTEST PASS surfaces, renderer, fonts, timer, mouse, keyboard\n");
  return 0;
}
