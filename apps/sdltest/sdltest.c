#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
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
  Uint64 deadline = SDL_GetTicks() + 10000;
  SDL_Event event;
  while (SDL_GetTicks() < deadline) {
    if (SDL_WaitEventTimeout(&event, 100) && event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_SPACE)
      return 0;
  }
  logkf("SDLFRAME FAIL timeout %s\n", phase);
  return 1;
}

static int test_image_io(void) {
  SDL_Surface *source = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_ARGB8888);
  CHECK(source);
  CHECK(SDL_FillSurfaceRect(source, NULL,
                            SDL_MapSurfaceRGBA(source, 24, 64, 128, 255)));
  SDL_IOStream *stream = SDL_IOFromDynamicMem();
  CHECK(stream && IMG_SavePNG_IO(source, stream, false));
  CHECK(SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET) == 0);
  SDL_Surface *decoded = IMG_Load_IO(stream, false);
  CHECK(decoded && decoded->w == 4 && decoded->h == 4);
  Uint8 r, g, b, a;
  CHECK(SDL_ReadSurfacePixel(decoded, 0, 0, &r, &g, &b, &a));
  CHECK(r == 24 && g == 64 && b == 128 && a == 255);
  SDL_DestroySurface(decoded);
  SDL_DestroySurface(source);
  CHECK(SDL_CloseIO(stream));

  stream = SDL_IOFromFile("/sdl3-io.tmp", "w+b");
  CHECK(stream);
  FILE *file = SDL_GetPointerProperty(
      SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, NULL);
  CHECK(file);
  CHECK(stream && SDL_WriteIO(stream, "SDL3", 4) == 4 && SDL_FlushIO(stream));
  CHECK(SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET) == 0);
  char data[8] = {0};
  CHECK(SDL_ReadIO(stream, data, sizeof(data)) == 4 &&
        strcmp(data, "SDL3") == 0);
  CHECK(SDL_GetIOStatus(stream) == SDL_IO_STATUS_EOF && feof(file));
  clearerr(file);
  CHECK(!feof(file) && !ferror(file));
  CHECK(SDL_CloseIO(stream) && remove("/sdl3-io.tmp") == 0);
  return 0;
}

static SDL_Window *create_window_at(const char *title, int x, int y, int w,
                                    int h) {
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props)
    return NULL;
  SDL_Window *window = NULL;
  if (SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                            title) &&
      SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x) &&
      SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y) &&
      SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w) &&
      SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h))
    window = SDL_CreateWindowWithProperties(props);
  SDL_DestroyProperties(props);
  return window;
}

static int expose_window(SDL_Window *window) {
  int x, y, width, height;
  SDL_GetWindowPosition(window, &x, &y);
  SDL_GetWindowSize(window, &width, &height);
  SDL_Window *cover = create_window_at("Exposure test", x, y, width, height);
  CHECK(cover);
  SDL_DestroyWindow(cover);
  return 0;
}

static int test_frame_publication(SDL_Window *first, SDL_Window *second,
                                  SDL_Surface *surface,
                                  SDL_Renderer *renderer) {
  /* Clearing the next frame must not change the presented image on exposure. */
  CHECK(SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255));
  CHECK(SDL_RenderClear(renderer));
  CHECK(SDL_FlushRenderer(renderer));
  CHECK(expose_window(second) == 0);
  CHECK(frame_checkpoint("draft") == 0);

  /* A partial update must leave all pixels outside the damage unchanged. */
  CHECK(SDL_FillSurfaceRect(surface, NULL,
                            SDL_MapSurfaceRGB(surface, 240, 160, 32)));
  SDL_Rect damage = {0, 80, 32, 32};
  CHECK(SDL_UpdateWindowSurfaceRects(first, &damage, 1));
  CHECK(expose_window(first) == 0);
  CHECK(frame_checkpoint("partial") == 0);

  /* Return from Present is the boundary for reusing the shared render buffer.
   */
  CHECK(SDL_SetRenderDrawColor(renderer, 224, 48, 32, 255));
  CHECK(SDL_RenderClear(renderer));
  CHECK(SDL_RenderPresent(renderer));
  CHECK(SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255));
  CHECK(SDL_RenderClear(renderer));
  CHECK(SDL_FlushRenderer(renderer));
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
  CHECK(SDL_Init(SDL_INIT_VIDEO));
  CHECK(SDL_VERSIONNUM_MAJOR(SDL_GetVersion()) == 3);
  CHECK(!SDL_InitSubSystem(SDL_INIT_AUDIO));
  CHECK(test_image_io() == 0);
  logkf("SDLTEST image IO passed\n");
  Uint64 start = SDL_GetPerformanceCounter();
  SDL_Delay(20);
  CHECK(SDL_GetPerformanceFrequency() == 1000000000);
  CHECK(SDL_GetPerformanceCounter() - start >= 10000000);
  CHECK(SDL_GetTicks() >= 10);
  const SDL_DisplayMode *mode =
      SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  framebuffer_info_t display;
  CHECK(framebuffer_info(&display) == 0);
  CHECK(mode);
  CHECK(mode->w == display.width && mode->h == display.height);

  SDL_Window *first = create_window_at("SDL surface", 80, 80, 256, 160);
  SDL_Window *second = create_window_at("SDL renderer", 420, 80, 128, 128);
  CHECK(first && second);
  CHECK(!SDL_SetWindowSize(first, 128, 128));
  SDL_Surface *surface = SDL_GetWindowSurface(first);
  CHECK(surface && surface->pitch >= surface->w * 4);
  CHECK(SDL_FillSurfaceRect(surface, NULL,
                            SDL_MapSurfaceRGB(surface, 24, 64, 128)));
  CHECK(SDL_UpdateWindowSurface(first));
  SDL_Rect clipped[] = {{-4, -4, 20, 20}, {0, 0, 0, 0}, {250, 155, 50, 50}};
  CHECK(SDL_UpdateWindowSurfaceRects(first, clipped, 3));
  CHECK(TTF_Init());
  TTF_Font *font = TTF_OpenFont("/data/fonts/mono.ttf", 18);
  CHECK(font);
  SDL_Surface *text = TTF_RenderText_Blended(font, "SDL3 / SSE2", 0,
                                             (SDL_Color){255, 255, 255, 255});
  CHECK(text);
  SDL_Rect label = {12, 16, 0, 0};
  CHECK(SDL_BlitSurface(text, NULL, surface, &label));
  SDL_DestroySurface(text);
  TTF_CloseFont(font);
  TTF_Quit();
  CHECK(SDL_UpdateWindowSurface(first));
  SDL_Renderer *renderer = SDL_CreateRenderer(second, "software");
  CHECK(renderer);
  CHECK(SDL_SetRenderDrawColor(renderer, 32, 192, 64, 255));
  CHECK(SDL_RenderClear(renderer));
  CHECK(SDL_RenderPresent(renderer));
  CHECK(SDL_StartTextInput(first));
  logkf("SDLTEST READY origin=%u,%u target=160,160\n", display.width / 2,
        display.height / 2);
  unsigned seen = 0;
  Uint64 deadline = SDL_GetTicks() + 20000;
  while (SDL_GetTicks() < deadline && seen != 255) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.x == 76 &&
          event.motion.y == 56)
        seen |= 1;
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          event.button.button == SDL_BUTTON_LEFT)
        seen |= 2;
      if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
          event.button.button == SDL_BUTTON_LEFT)
        seen |= 4;
      if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y == 1)
        seen |= 8;
      if (event.type == SDL_EVENT_TEXT_INPUT &&
          strcmp(event.text.text, "a") == 0)
        seen |= 16;
      if (event.type == SDL_EVENT_TEXT_INPUT &&
          strcmp(event.text.text, "A") == 0)
        seen |= 128;
      if (event.type == SDL_EVENT_KEY_DOWN &&
          event.key.scancode == SDL_SCANCODE_LEFT)
        seen |= 32;
      if (event.type == SDL_EVENT_KEY_UP &&
          event.key.scancode == SDL_SCANCODE_LEFT)
        seen |= 64;
    }
    SDL_Delay(10);
  }
  logkf("SDLTEST INPUT seen=%u\n", seen);
  CHECK(seen == 255);
  CHECK(test_frame_publication(first, second, surface, renderer) == 0);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(second);
  SDL_DestroyWindow(first);
  SDL_Quit();
  logkf("SDLTEST PASS SDL3 surfaces, renderer, image IO, fonts, timer, mouse, "
        "keyboard\n");
  return 0;
}
