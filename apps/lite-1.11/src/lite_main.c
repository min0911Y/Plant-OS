#include "api/api.h"
#include "renderer.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

SDL_Window *window;


static double get_scale(void) {
  return 1.0;
}


static void get_exe_filename(char *buf, int sz) {
  strcpy(buf, "lite");
}


int main(int argc, char **argv) {

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "lite: %s\n", SDL_GetError());
    return 1;
  }
  SDL_EnableScreenSaver();
  SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
  atexit(SDL_Quit);

  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

  const SDL_DisplayMode *dm =
      SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  if (!dm)
    return 1;

  window = SDL_CreateWindow("Lite", dm->w * 0.8, dm->h * 0.8, 0);
  if (!window) {
    fprintf(stderr, "lite: %s\n", SDL_GetError());
    return 1;
  }
  SDL_StartTextInput(window);
  ren_init(window);


  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  api_load_libs(L);


  lua_newtable(L);
  for (int i = 0; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i + 1);
  }
  lua_setglobal(L, "ARGS");

  lua_pushstring(L, "1.11");
  lua_setglobal(L, "VERSION");

  lua_pushstring(L, SDL_GetPlatform());
  lua_setglobal(L, "PLATFORM");

  lua_pushnumber(L, get_scale());
  lua_setglobal(L, "SCALE");

  char exename[2048];
  get_exe_filename(exename, sizeof(exename));
  lua_pushstring(L, exename);
  lua_setglobal(L, "EXEFILE");


  (void) luaL_dostring(L,
    "local core\n"
    "xpcall(function()\n"
    "  SCALE = SCALE\n"
    "  PATHSEP = package.config:sub(1, 1)\n"
    "  EXEDIR = ''\n"
    "  package.path = '/data/?.lua;' .. package.path\n"
    "  package.path = '/data/?/init.lua;' .. package.path\n"
    "  core = require('core')\n"
    "  core.init()\n"
    "  core.run()\n"
    "end, function(err)\n"
    "  print('Error: ' .. tostring(err))\n"
    "  print(debug.traceback(nil, 2))\n"
    "  if core and core.on_error then\n"
    "    pcall(core.on_error, err)\n"
    "  end\n"
    "  os.exit(1)\n"
    "end)");


  lua_close(L);
  SDL_DestroyWindow(window);

  return EXIT_SUCCESS;
}
