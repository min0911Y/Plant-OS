/* nuklear - 1.32.0 - public domain */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_sdl_renderer.h"

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

/* ===============================================================
 *
 *                          EXAMPLE
 *
 * ===============================================================*/
/* This are some code examples to provide a small overview of what can be
 * done with this library. To try out an example uncomment the defines */
// #define INCLUDE_ALL
/*#define INCLUDE_STYLE */
#define INCLUDE_CALCULATOR
/*#define INCLUDE_CANVAS */
/*#define INCLUDE_OVERVIEW */
/*#define INCLUDE_NODE_EDITOR */

#ifdef INCLUDE_ALL
#define INCLUDE_STYLE
#define INCLUDE_CALCULATOR
#define INCLUDE_CANVAS
#define INCLUDE_OVERVIEW
#define INCLUDE_NODE_EDITOR
#endif

#ifdef INCLUDE_STYLE
#include "style.c"
#endif
#ifdef INCLUDE_CALCULATOR
#include "calculator.c"
#endif
#ifdef INCLUDE_CANVAS
#include "canvas.c"
#endif
#ifdef INCLUDE_OVERVIEW
#include "overview.c"
#endif
#ifdef INCLUDE_NODE_EDITOR
#include "node_editor.c"
#endif

/* ===============================================================
 *
 *                          DEMO
 *
 * ===============================================================*/

int start = 0;
nk_size prog = 0;
void thread() __attribute__((optimize("O0")));
void thread() {
  while (1) {
    if (start) {
      if (prog < 100) {
        prog++;
        sleep(10);
      }
      
    }
  }
}
int main(int argc, char *argv[]) {
  /* Platform */
  SDL_Window *win;
  SDL_Renderer *renderer;
  int running = 1;
  int flags = 0;
  float font_scale = 1;

  /* GUI */
  struct nk_context *ctx;
  struct nk_colorf bg;

  /* SDL setup */
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_Init(SDL_INIT_VIDEO);

  win = SDL_CreateWindow("Demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT,
                         SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

  if (win == NULL) {
    SDL_Log("Error SDL_CreateWindow %s", SDL_GetError());
    exit(-1);
  }

  flags |= SDL_RENDERER_ACCELERATED;
  flags |= SDL_RENDERER_PRESENTVSYNC;

#if 0
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif

  renderer = SDL_CreateRenderer(win, -1, 0);

  if (renderer == NULL) {
    SDL_Log("Error SDL_CreateRenderer %s", SDL_GetError());
    exit(-1);
  }

  /* scale the renderer output for High-DPI displays */
  {
    int render_w, render_h;
    int window_w, window_h;
    float scale_x, scale_y;
    SDL_GetRendererOutputSize(renderer, &render_w, &render_h);
    SDL_GetWindowSize(win, &window_w, &window_h);
    scale_x = (float)(render_w) / (float)(window_w);
    scale_y = (float)(render_h) / (float)(window_h);
    SDL_RenderSetScale(renderer, scale_x, scale_y);
    font_scale = scale_y;
  }

  /* GUI */
  ctx = nk_sdl_init(win, renderer);
  /* Load Fonts: if none of these are loaded a default font will be used  */
  /* Load Cursor: if you uncomment cursor loading please hide the cursor */
  {
    struct nk_font_atlas *atlas;
    struct nk_font_config config = nk_font_config(0);
    struct nk_font *font;

    /* set up the font atlas and add desired font; note that font sizes are
     * multiplied by font_scale to produce better results at higher DPIs */
    nk_sdl_font_stash_begin(&atlas);
    // font = nk_font_atlas_add_default(atlas, 13 * font_scale, &config);
    font = nk_font_atlas_add_from_file(atlas, "data/fonts/mono.ttf",
                                       17 * font_scale, &config);
    /*font = nk_font_atlas_add_from_file(atlas,
     * "../../../extra_font/Roboto-Regular.ttf", 16 * font_scale, &config);*/
    /*font = nk_font_atlas_add_from_file(atlas,
     * "../../../extra_font/kenvector_future_thin.ttf", 13 * font_scale,
     * &config);*/
    /*font = nk_font_atlas_add_from_file(atlas,
     * "../../../extra_font/ProggyClean.ttf", 12 * font_scale, &config);*/
    /*font = nk_font_atlas_add_from_file(atlas,
     * "../../../extra_font/ProggyTiny.ttf", 10 * font_scale, &config);*/
    /*font = nk_font_atlas_add_from_file(atlas,
     * "../../../extra_font/Cousine-Regular.ttf", 13 * font_scale, &config);*/
    nk_sdl_font_stash_end();

    /* this hack makes the font appear to be scaled down to the desired
     * size and is only necessary when font_scale > 1 */
    font->handle.height /= font_scale;
    /*nk_style_load_all_cursors(ctx, atlas->cursors);*/
    nk_style_set_font(ctx, &font->handle);
  }

  bg.r = 0.10f, bg.g = 0.18f, bg.b = 0.24f, bg.a = 1.0f;
  char buffer[255] = {0};
  unsigned len = 0;
  int page = 1;
  nk_size val = 0;
  static const float ratio[] = {120, 150};
  start = 0;
  prog = 0;
  while (running) {
    /* Input */
    SDL_Event evt;
    nk_input_begin(ctx);
    while (SDL_PollEvent(&evt)) {
      if (evt.type == SDL_QUIT)
        goto cleanup;
      nk_sdl_handle_event(&evt);
    }
    nk_sdl_handle_grab(); /* optional grabbing behavior */
    nk_input_end(ctx);

    /* GUI */
    if (nk_begin(ctx, "Plant OS Setup",
                 nk_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
                 NK_WINDOW_NO_SCROLLBAR)) {
      // enum {EASY, HARD};
      // static int op = EASY;
      // static int property = 20;

      if (page) {
        nk_layout_row_static(ctx, 30, WINDOW_WIDTH, 1);
        nk_label(ctx, "Would you like to install Plant OS in drive C?",
                 NK_TEXT_CENTERED);
        nk_layout_row_static(ctx, 30, 80, 1);
        if (nk_button_label(ctx, "Yes")) {
          page = !page;
          AddThread("",thread,(unsigned)malloc(16*1024)+16*1024);
          start =1;
        }
      } else {
        nk_layout_row(ctx, NK_STATIC, 30, 2, ratio);
        nk_label(ctx, "Installing: ", NK_TEXT_CENTERED);
        nk_progress(ctx, &prog, 100, NK_MODIFIABLE);
        if (nk_button_label(ctx, "Back"))
          page = !page;
      }
      // nk_layout_row_dynamic(ctx, 30, 2);
      //  if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
      //  if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;
      //  nk_layout_row_dynamic(ctx, 25, 1);
      //  nk_property_int(ctx, "Compression:", 0, &property, 100, 10, 1);

      // nk_layout_row_dynamic(ctx, 20, 1);
      // nk_label(ctx, "background:", NK_TEXT_LEFT);
      // nk_layout_row_dynamic(ctx, 25, 1);
      // if (nk_combo_begin_color(ctx, nk_rgb_cf(bg),
      // nk_vec2(nk_widget_width(ctx),400))) {
      //     nk_layout_row_dynamic(ctx, 120, 1);
      //     bg = nk_color_picker(ctx, bg, NK_RGBA);
      //     nk_layout_row_dynamic(ctx, 25, 1);
      //     bg.r = nk_propertyf(ctx, "#R:", 0, bg.r, 1.0f, 0.01f,0.005f);
      //     bg.g = nk_propertyf(ctx, "#G:", 0, bg.g, 1.0f, 0.01f,0.005f);
      //     bg.b = nk_propertyf(ctx, "#B:", 0, bg.b, 1.0f, 0.01f,0.005f);
      //     bg.a = nk_propertyf(ctx, "#A:", 0, bg.a, 1.0f, 0.01f,0.005f);
      //     nk_combo_end(ctx);
      // }
      // nk_edit_string(ctx, NK_EDIT_SIMPLE, buffer, &len, 64,
      // nk_filter_default);
    }
    nk_end(ctx);

/* -------------- EXAMPLES ---------------- */
#ifdef INCLUDE_CALCULATOR
    // calculator(ctx);
#endif
#ifdef INCLUDE_CANVAS
    canvas(ctx);
#endif
#ifdef INCLUDE_OVERVIEW
    overview(ctx);
#endif
#ifdef INCLUDE_NODE_EDITOR
    node_editor(ctx);
#endif
    /* ----------------------------------------- */

    SDL_SetRenderDrawColor(renderer, bg.r * 255, bg.g * 255, bg.b * 255,
                           bg.a * 255);
    SDL_RenderClear(renderer);

    nk_sdl_render(NK_ANTI_ALIASING_ON);

    SDL_RenderPresent(renderer);
  }

cleanup:
  nk_sdl_shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
