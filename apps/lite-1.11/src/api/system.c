#include "api.h"
#include "rencache.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#ifdef _WIN32
#include <windows.h>
#endif

extern SDL_Window *window;

static const char *button_name(int button) {
  switch (button) {
  case 1:
    return "left";
  case 2:
    return "middle";
  case 3:
    return "right";
  default:
    return "?";
  }
}

static char *key_name(char *dst, int sym) {
  strcpy(dst, SDL_GetKeyName(sym));
  char *p = dst;
  while (*p) {
    *p = tolower(*p);
    p++;
  }
  return dst;
}

static int f_poll_event(lua_State *L) {
  char buf[16];
  int wx, wy;
  float mx, my;
  SDL_Event e;

top:
  if (!SDL_PollEvent(&e)) {
    return 0;
  }

  switch (e.type) {
  case SDL_EVENT_QUIT:
    lua_pushstring(L, "quit");
    return 1;

  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    lua_pushstring(L, "quit");
    return 1;
  case SDL_EVENT_WINDOW_RESIZED:
    lua_pushstring(L, "resized");
    lua_pushnumber(L, e.window.data1);
    lua_pushnumber(L, e.window.data2);
    return 3;
  case SDL_EVENT_WINDOW_EXPOSED:
    rencache_invalidate();
    lua_pushstring(L, "exposed");
    return 1;
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    SDL_FlushEvent(SDL_EVENT_KEY_DOWN);
    goto top;

  case SDL_EVENT_DROP_FILE:
    SDL_GetGlobalMouseState(&mx, &my);
    SDL_GetWindowPosition(window, &wx, &wy);
    lua_pushstring(L, "filedropped");
    lua_pushstring(L, e.drop.data);
    lua_pushnumber(L, mx - wx);
    lua_pushnumber(L, my - wy);
    return 4;

  case SDL_EVENT_KEY_DOWN:
    lua_pushstring(L, "keypressed");
    lua_pushstring(L, key_name(buf, e.key.key));
    return 2;

  case SDL_EVENT_KEY_UP:
    lua_pushstring(L, "keyreleased");
    lua_pushstring(L, key_name(buf, e.key.key));
    return 2;

  case SDL_EVENT_TEXT_INPUT:
    lua_pushstring(L, "textinput");
    lua_pushstring(L, e.text.text);
    return 2;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (e.button.button == 1) {
      SDL_CaptureMouse(1);
    }
    lua_pushstring(L, "mousepressed");
    lua_pushstring(L, button_name(e.button.button));
    lua_pushnumber(L, e.button.x);
    lua_pushnumber(L, e.button.y);
    lua_pushnumber(L, e.button.clicks);
    return 5;

  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (e.button.button == 1) {
      SDL_CaptureMouse(0);
    }
    lua_pushstring(L, "mousereleased");
    lua_pushstring(L, button_name(e.button.button));
    lua_pushnumber(L, e.button.x);
    lua_pushnumber(L, e.button.y);
    return 4;

  case SDL_EVENT_MOUSE_MOTION:
    lua_pushstring(L, "mousemoved");
    lua_pushnumber(L, e.motion.x);
    lua_pushnumber(L, e.motion.y);
    lua_pushnumber(L, e.motion.xrel);
    lua_pushnumber(L, e.motion.yrel);
    return 5;

  case SDL_EVENT_MOUSE_WHEEL:
    lua_pushstring(L, "mousewheel");
    lua_pushnumber(L, e.wheel.y);
    return 2;

  default:
    goto top;
  }

  return 0;
}

static int f_wait_event(lua_State *L) {
  double n = luaL_checknumber(L, 1);
  lua_pushboolean(L, SDL_WaitEventTimeout(NULL, n * 1000));
  return 1;
}

static SDL_Cursor *cursor_cache[SDL_SYSTEM_CURSOR_POINTER + 1];

static const char *cursor_opts[] = {"arrow", "ibeam", "sizeh",
                                    "sizev", "hand",  NULL};

static const int cursor_enums[] = {
    SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT,
    SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE,
    SDL_SYSTEM_CURSOR_POINTER};

static int f_set_cursor(lua_State *L) {
  int opt = luaL_checkoption(L, 1, "arrow", cursor_opts);
  int n = cursor_enums[opt];
  // SDL_Cursor *cursor = cursor_cache[n];
  // if (!cursor) {
  //   cursor = SDL_CreateSystemCursor(n);
  //   cursor_cache[n] = cursor;
  // }
  // SDL_SetCursor(cursor);
  return 0;
}

static int f_set_window_title(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);
  SDL_SetWindowTitle(window, title);
  return 0;
}

static const char *window_opts[] = {"normal", "maximized", "fullscreen", 0};
enum { WIN_NORMAL, WIN_MAXIMIZED, WIN_FULLSCREEN };

static int f_set_window_mode(lua_State *L) {
  int n = luaL_checkoption(L, 1, "normal", window_opts);
  SDL_SetWindowFullscreen(window, n == WIN_FULLSCREEN);
  if (n == WIN_NORMAL) {
    SDL_RestoreWindow(window);
  }
  if (n == WIN_MAXIMIZED) {
    SDL_MaximizeWindow(window);
  }
  return 0;
}

static int f_window_has_focus(lua_State *L) {
  unsigned flags = SDL_GetWindowFlags(window);
  lua_pushboolean(L, 1);
  return 1;
}

static int f_show_confirm_dialog(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);
  const char *msg = luaL_checkstring(L, 2);

#if _WIN32
  int id = MessageBox(0, msg, title, MB_YESNO | MB_ICONWARNING);
  lua_pushboolean(L, id == IDYES);

#else
  SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
  };
  SDL_MessageBoxData data = {
      .title = title,
      .message = msg,
      .numbuttons = 2,
      .buttons = buttons,
  };
  int buttonid;
  SDL_ShowMessageBox(&data, &buttonid);
  lua_pushboolean(L, buttonid == 1);
#endif
  return 1;
}

static int f_chdir(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  if (strcmp(path, "") == 0)
    return 0;
  int err = chdir(path);
  if (err != 0) {
    luaL_error(L, "chdir() failed");
  }
  return 0;
}

static int f_list_dir(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);

  struct finfo_block *f;
  size_t count;
  if (list_directory(path, &f, &count) != 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_newtable(L);
  int i = 1;

  for (size_t j = 0; j < count; j++) {
    if (strcmp(f[j].name, ".") == 0) {
      continue;
    }
    if (strcmp(f[j].name, "..") == 0) {
      continue;
    }
    lua_pushstring(L, f[j].name);
    lua_rawseti(L, -2, i);
    i++;
  }

  free(f);
  return 1;
}

static int f_absolute_path(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  if (path[0] == '/' || (path[0] && path[1] == ':')) {
    lua_pushstring(L, path);
    return 1;
  }
  size_t capacity = 128;
  char *cwd = NULL;
  for (;;) {
    char *grown = realloc(cwd, capacity);
    if (!grown) {
      free(cwd);
      return luaL_error(L, "path allocation failed");
    }
    cwd = grown;
    if (getcwd(cwd, capacity))
      break;
    if (errno != ERANGE || capacity > (size_t)-1 / 2) {
      free(cwd);
      return luaL_error(L, "getcwd() failed: %s", strerror(errno));
    }
    capacity *= 2;
  }
  lua_pushfstring(L, "%s%s%s", cwd, cwd[strlen(cwd) - 1] == '/' ? "" : "/", path);
  free(cwd);
  return 1;
}

static int f_get_file_info(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  struct stat status;
  if (stat(path, &status) != 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  lua_newtable(L);
  lua_pushnumber(L, status.st_mtime);
  lua_setfield(L, -2, "modified");
  lua_pushnumber(L, status.st_size);
  lua_setfield(L, -2, "size");
  lua_pushstring(L, S_ISDIR(status.st_mode) ? "dir" : "file");
  lua_setfield(L, -2, "type");
  return 1;
}

static int f_get_clipboard(lua_State *L) {
  char *text = SDL_GetClipboardText();
  if (!text) {
    return 0;
  }
  lua_pushstring(L, text);
  SDL_free(text);
  return 1;
}

static int f_set_clipboard(lua_State *L) {
  const char *text = luaL_checkstring(L, 1);
  SDL_SetClipboardText(text);
  return 0;
}

static int f_get_time(lua_State *L) {
  double n =
      SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
  lua_pushnumber(L, n);
  return 1;
}

static int f_sleep(lua_State *L) {
  double n = luaL_checknumber(L, 1);
  SDL_Delay(n * 1000);
  return 0;
}

static int f_exec(lua_State *L) {
  size_t len;
  const char *cmd = luaL_checklstring(L, 1, &len);
  char *buf = malloc(len + 32);
  if (!buf) {
    luaL_error(L, "buffer allocation failed");
  }
#if _WIN32
  sprintf(buf, "cmd /c \"%s\"", cmd);
  WinExec(buf, SW_HIDE);
#else
  sprintf(buf, "%s &", cmd);
  int res = system(buf);
  (void)res;
#endif
  free(buf);
  return 0;
}

static int f_fuzzy_match(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  const char *ptn = luaL_checkstring(L, 2);
  int score = 0;
  int run = 0;

  while (*str && *ptn) {
    while (*str == ' ') {
      str++;
    }
    while (*ptn == ' ') {
      ptn++;
    }
    if (tolower(*str) == tolower(*ptn)) {
      score += run * 10 - (*str != *ptn);
      run++;
      ptn++;
    } else {
      score -= 10;
      run = 0;
    }
    str++;
  }
  if (*ptn) {
    return 0;
  }

  lua_pushnumber(L, score - (int)strlen(str));
  return 1;
}

static const luaL_Reg lib[] = {{"poll_event", f_poll_event},
                               {"wait_event", f_wait_event},
                               {"set_cursor", f_set_cursor},
                               {"set_window_title", f_set_window_title},
                               {"set_window_mode", f_set_window_mode},
                               {"window_has_focus", f_window_has_focus},
                               {"show_confirm_dialog", f_show_confirm_dialog},
                               {"chdir", f_chdir},
                               {"list_dir", f_list_dir},
                               {"absolute_path", f_absolute_path},
                               {"get_file_info", f_get_file_info},
                               {"get_clipboard", f_get_clipboard},
                               {"set_clipboard", f_set_clipboard},
                               {"get_time", f_get_time},
                               {"sleep", f_sleep},
                               {"exec", f_exec},
                               {"fuzzy_match", f_fuzzy_match},
                               {NULL, NULL}};

int luaopen_system(lua_State *L) {
  luaL_newlib(L, lib);
  return 1;
}
