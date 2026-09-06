#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

int main(int argc, char **argv) {
  int status = 1;
  SDL_Window *window = NULL;
  SDL_Renderer *renderer = NULL;
  TTF_Font *font = NULL;
  SDL_Surface *surface = NULL;
  SDL_Texture *textures[2] = {NULL, NULL};
  SDL_FRect destinations[2];
  const char *messages[] = {"Genshin Impact, Start!",
                            "\xe4\xbd\xa0\xe5\xa5\xbd,TTF"};
  if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init())
    goto done;
  window = SDL_CreateWindow("SDL text", 800, 640, 0);
  if (!window)
    goto done;
  renderer = SDL_CreateRenderer(window, "software");
  font = TTF_OpenFont(argc > 1 ? argv[1] : "/data/fonts/mono.ttf", 60);
  if (!renderer || !font)
    goto done;
  for (int i = 0; i < 2; i++) {
    surface = TTF_RenderText_Blended(font, messages[i], 0,
                                     (SDL_Color){52, 203, 120, 255});
    if (!surface)
      goto done;
    textures[i] = SDL_CreateTextureFromSurface(renderer, surface);
    destinations[i] = (SDL_FRect){(800 - surface->w) / 2.0f, 100 + i * 80,
                                  surface->w, surface->h};
    SDL_DestroySurface(surface);
    surface = NULL;
    if (!textures[i])
      goto done;
  }
  if (!SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) ||
      !SDL_RenderClear(renderer))
    goto done;
  for (int i = 0; i < 2; i++) {
    if (!SDL_RenderTexture(renderer, textures[i], NULL, &destinations[i]))
      goto done;
  }
  if (!SDL_RenderPresent(renderer))
    goto done;
  SDL_Event event;
  while (SDL_WaitEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT ||
        event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
      status = 0;
      break;
    }
  }
done:
  if (status)
    SDL_Log("bitz: %s", SDL_GetError());
  SDL_DestroySurface(surface);
  for (int i = 0; i < 2; i++)
    SDL_DestroyTexture(textures[i]);
  TTF_CloseFont(font);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();
  return status;
}
