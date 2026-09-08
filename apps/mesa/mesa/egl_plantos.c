// SPDX-License-Identifier: MIT
#include "eglconfig.h"
#include "eglcontext.h"
#include "eglcurrent.h"
#include "egldisplay.h"
#include "egldriver.h"
#include "eglsurface.h"
#include "frontend/sw_winsys.h"
#include "llvmpipe/lp_public.h"
#include "pipe/p_context.h"
#include "plant_egl.h"
#include "state_tracker/st_context.h"
#include "sw/null/null_sw_winsys.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h"
#include <limits.h>

struct plant_display {
  struct pipe_frontend_screen frontend;
  struct sw_winsys *winsys;
  mtx_t context_mutex;
  int references;
};

struct plant_context {
  _EGLContext base;
  struct st_context *st;
};

struct plant_surface {
  _EGLSurface base;
  struct pipe_frontend_drawable drawable;
  struct st_visual visual;
  struct plant_egl_window window;
  struct pipe_resource *textures[ST_ATTACHMENT_COUNT];
};

static uint32_t drawable_id;
static simple_mtx_t binding_mutex = SIMPLE_MTX_INITIALIZER;

static struct st_visual config_visual(const _EGLConfig *config) {
  return (struct st_visual){
      .buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK |
                     ST_ATTACHMENT_BACK_LEFT_MASK |
                     (config->DepthSize ? ST_ATTACHMENT_DEPTH_STENCIL_MASK : 0),
      .color_format = PIPE_FORMAT_B8G8R8A8_UNORM,
      .depth_stencil_format =
          config->DepthSize ? PIPE_FORMAT_Z24_UNORM_S8_UINT : PIPE_FORMAT_NONE,
  };
}

static bool window_buffer(const struct plant_egl_window *window,
                          window_buffer_t *buffer) {
  return window_get_buffer(window->window, buffer) == 0 && buffer->pixels &&
         buffer->width <= INT16_MAX && buffer->height <= INT16_MAX &&
         buffer->pitch >= (size_t)buffer->width * 4 && window->width &&
         window->height && window->x < buffer->width &&
         window->y < buffer->height &&
         window->width <= buffer->width - window->x &&
         window->height <= buffer->height - window->y;
}

static void display_release(_EGLDisplay *display) {
  struct plant_display *native = display->DriverData;
  if (!p_atomic_dec_zero(&native->references))
    return;
  st_screen_destroy(&native->frontend);
  native->frontend.screen->destroy(native->frontend.screen);
  native->winsys->destroy(native->winsys);
  mtx_destroy(&native->context_mutex);
  _eglCleanupDisplay(display);
  display->DriverData = NULL;
  free(native);
}

static int screen_param(struct pipe_frontend_screen *screen,
                        enum st_manager_param param) {
  return 0;
}

static EGLBoolean plant_initialize(_EGLDisplay *display) {
  if ((display->Platform != _EGL_PLATFORM_PLANTOS &&
       display->Platform != _EGL_PLATFORM_SURFACELESS) ||
      display->PlatformDisplay)
    return _eglError(EGL_NOT_INITIALIZED, "unsupported native display");
  struct plant_display *native = display->DriverData;
  if (native) {
    p_atomic_inc(&native->references);
    return EGL_TRUE;
  }
  native = calloc(1, sizeof(*native));
  if (!native)
    return _eglError(EGL_BAD_ALLOC, "eglInitialize");
  if (mtx_init(&native->context_mutex, mtx_plain) != thrd_success) {
    free(native);
    return _eglError(EGL_BAD_ALLOC, "eglInitialize mutex");
  }
  native->winsys = null_sw_create();
  native->frontend.screen =
      native->winsys ? llvmpipe_create_screen(native->winsys) : NULL;
  if (!native->frontend.screen) {
    if (native->winsys)
      native->winsys->destroy(native->winsys);
    mtx_destroy(&native->context_mutex);
    free(native);
    return _eglError(EGL_BAD_ALLOC, "llvmpipe screen");
  }
  native->frontend.get_param = screen_param;
  native->references = 1;
  /* Initialization is serialized by EGL; failure removes all configuration. */
  display->DriverData = native;
  display->ClientAPIs = EGL_OPENGL_BIT;
  display->Extensions.KHR_create_context = EGL_TRUE;
  display->Extensions.KHR_surfaceless_context = EGL_TRUE;
  display->Extensions.KHR_get_all_proc_addresses = EGL_TRUE;
  unsigned limit = native->frontend.screen->caps.max_texture_2d_size;
  for (unsigned depth = 0; depth < 2; depth++) {
    _EGLConfig *config = calloc(1, sizeof(*config));
    if (!config)
      goto fail;
    _eglInitConfig(config, display, depth + 1);
    config->RedSize = config->GreenSize = config->BlueSize = config->AlphaSize =
        8;
    config->BufferSize = 32;
    config->DepthSize = depth ? 24 : 0;
    config->StencilSize = depth ? 8 : 0;
    config->SurfaceType =
        EGL_PBUFFER_BIT |
        (display->Platform == _EGL_PLATFORM_PLANTOS ? EGL_WINDOW_BIT : 0);
    config->RenderableType = config->Conformant = EGL_OPENGL_BIT;
    config->NativeRenderable = EGL_TRUE;
    config->MaxPbufferWidth = config->MaxPbufferHeight = MIN2(limit, INT_MAX);
    config->MaxPbufferPixels = MIN2((uint64_t)limit * limit, INT_MAX);
    config->MinSwapInterval = config->MaxSwapInterval = 0;
    if (!_eglValidateConfig(config, EGL_FALSE) || !_eglLinkConfig(config)) {
      free(config);
      goto fail;
    }
  }
  return EGL_TRUE;
fail:
  display_release(display);
  return _eglError(EGL_BAD_ALLOC, "EGL configurations");
}

static EGLBoolean plant_terminate(_EGLDisplay *display) {
  _eglReleaseDisplayResources(display);
  display_release(display);
  return EGL_TRUE;
}

static _EGLContext *plant_create_context(_EGLDisplay *display,
                                         _EGLConfig *config, _EGLContext *share,
                                         const EGLint *list) {
  struct plant_display *native = display->DriverData;
  struct plant_context *context = calloc(1, sizeof(*context));
  if (!context) {
    _eglError(EGL_BAD_ALLOC, "eglCreateContext");
    return NULL;
  }
  if (!_eglInitContext(&context->base, display, config, share, list)) {
    free(context);
    return NULL;
  }
  _EGLContext *base = &context->base;
  bool core =
      (base->ClientMajorVersion == 3 && base->ClientMinorVersion == 1) ||
      ((base->ClientMajorVersion > 3 ||
        (base->ClientMajorVersion == 3 && base->ClientMinorVersion >= 2)) &&
       base->Profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR) ||
      (base->Flags & EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR);
  struct st_context_attribs attribs = {
      .profile = core ? API_OPENGL_CORE : API_OPENGL_COMPAT,
      .major = base->ClientMajorVersion,
      .minor = base->ClientMinorVersion,
      .visual = config_visual(config),
  };
  if (base->Flags & EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR)
    attribs.flags |= ST_CONTEXT_FLAG_DEBUG;
  if (base->Flags & EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR)
    attribs.flags |= ST_CONTEXT_FLAG_FORWARD_COMPATIBLE;
  if (base->Flags & EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR)
    attribs.context_flags |= PIPE_CONTEXT_ROBUST_BUFFER_ACCESS;
  if (base->ResetNotificationStrategy == EGL_LOSE_CONTEXT_ON_RESET_KHR)
    attribs.context_flags |= PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET;
  enum st_context_error error;
  mtx_lock(&native->context_mutex);
  context->st =
      st_api_create_context(&native->frontend, &attribs, &error,
                            share ? ((struct plant_context *)share)->st : NULL);
  mtx_unlock(&native->context_mutex);
  if (!context->st) {
    free(context);
    _eglError(error == ST_CONTEXT_ERROR_BAD_VERSION ? EGL_BAD_MATCH
                                                    : EGL_BAD_ALLOC,
              "OpenGL context");
    return NULL;
  }
  p_atomic_inc(&native->references);
  return base;
}

static EGLBoolean plant_destroy_context(_EGLDisplay *display,
                                        _EGLContext *base) {
  if (_eglPutContext(base)) {
    display = base->Resource.Display;
    st_destroy_context(((struct plant_context *)base)->st);
    free(base);
    display_release(display);
  }
  return EGL_TRUE;
}

static bool surface_validate(struct st_context *st,
                             struct pipe_frontend_drawable *drawable,
                             const enum st_attachment_type *attachments,
                             unsigned count, struct pipe_resource **out,
                             struct pipe_resource **resolve) {
  struct plant_surface *surface =
      container_of(drawable, struct plant_surface, drawable);
  for (unsigned i = 0; i < count; i++)
    pipe_resource_reference(&out[i], surface->textures[attachments[i]]);
  return true;
}

static bool surface_present(struct st_context *st,
                            struct plant_surface *surface,
                            enum st_attachment_type attachment) {
  if (surface->base.Type != EGL_WINDOW_BIT)
    return true;
  window_buffer_t buffer;
  const struct plant_egl_window *window = &surface->window;
  if (!window_buffer(window, &buffer))
    return false;
  struct pipe_transfer *transfer = NULL;
  const uint8_t *pixels = pipe_texture_map(
      st->pipe, surface->textures[attachment], 0, 0, PIPE_MAP_READ, 0, 0,
      window->width, window->height, &transfer);
  if (!pixels)
    return false;
  uint8_t *destination = (uint8_t *)buffer.pixels +
                         window->y * (size_t)buffer.pitch + window->x * 4;
  for (uint32_t y = 0; y < window->height; y++)
    memcpy(destination + y * (size_t)buffer.pitch,
           pixels + y * (size_t)transfer->stride, (size_t)window->width * 4);
  pipe_texture_unmap(st->pipe, transfer);
  return window_present(window->window, (window->x << 16) | window->y,
                        ((window->x + window->width) << 16) |
                            (window->y + window->height)) == 0;
}

static bool surface_flush_front(struct st_context *st,
                                struct pipe_frontend_drawable *drawable,
                                enum st_attachment_type attachment) {
  struct plant_surface *surface =
      container_of(drawable, struct plant_surface, drawable);
  return attachment == ST_ATTACHMENT_FRONT_LEFT &&
         surface_present(st, surface, attachment);
}

static EGLBoolean plant_destroy_surface(_EGLDisplay *display,
                                        _EGLSurface *base) {
  if (_eglPutSurface(base)) {
    struct plant_surface *surface = (void *)base;
    display = base->Resource.Display;
    st_api_destroy_drawable(&surface->drawable);
    for (unsigned i = 0; i < ST_ATTACHMENT_COUNT; i++)
      pipe_resource_reference(&surface->textures[i], NULL);
    free(surface);
    display_release(display);
  }
  return EGL_TRUE;
}

static _EGLSurface *create_surface(_EGLDisplay *display, _EGLConfig *config,
                                   const struct plant_egl_window *window,
                                   const EGLint *list) {
  struct plant_display *native = display->DriverData;
  struct plant_surface *surface = calloc(1, sizeof(*surface));
  if (!surface) {
    _eglError(EGL_BAD_ALLOC, "EGL surface");
    return NULL;
  }
  _EGLSurface *base = &surface->base;
  if (!_eglInitSurface(base, display, window ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
                       config, list, NULL)) {
    free(surface);
    return NULL;
  }
  EGLint error = EGL_BAD_ALLOC;
  if (window) {
    window_buffer_t buffer;
    if (!window_buffer(window, &buffer)) {
      error = EGL_BAD_NATIVE_WINDOW;
      goto fail;
    }
    surface->window = *window;
    base->NativeSurface = (void *)window;
    base->Width = window->width;
    base->Height = window->height;
  }
  if (base->Width > config->MaxPbufferWidth ||
      base->Height > config->MaxPbufferHeight)
    goto fail;
  surface->visual = config_visual(config);
  if (base->Type == EGL_PBUFFER_BIT ||
      base->ActiveRenderBuffer == EGL_SINGLE_BUFFER)
    surface->visual.buffer_mask &= ~ST_ATTACHMENT_BACK_LEFT_MASK;
  surface->drawable = (struct pipe_frontend_drawable){
      .stamp = 1,
      .ID = p_atomic_inc_return(&drawable_id),
      .fscreen = &native->frontend,
      .visual = &surface->visual,
      .validate = surface_validate,
      .flush_front = surface_flush_front,
  };
  struct pipe_resource resource = {
      .target = PIPE_TEXTURE_2D,
      .width0 = MAX2(base->Width, 1),
      .height0 = MAX2(base->Height, 1),
      .depth0 = 1,
      .array_size = 1,
  };
  struct pipe_screen *screen = native->frontend.screen;
  for (unsigned i = 0; i < ST_ATTACHMENT_COUNT; i++) {
    if (!(surface->visual.buffer_mask & (1u << i)))
      continue;
    bool depth = i == ST_ATTACHMENT_DEPTH_STENCIL;
    resource.format = depth ? surface->visual.depth_stencil_format
                            : surface->visual.color_format;
    resource.bind = depth ? PIPE_BIND_DEPTH_STENCIL : PIPE_BIND_RENDER_TARGET;
    surface->textures[i] = screen->resource_create(screen, &resource);
    if (!surface->textures[i])
      goto fail;
  }
  p_atomic_inc(&native->references);
  return base;
fail:
  for (unsigned i = 0; i < ST_ATTACHMENT_COUNT; i++)
    pipe_resource_reference(&surface->textures[i], NULL);
  free(surface);
  _eglError(error, "EGL surface buffers");
  return NULL;
}

static _EGLSurface *plant_create_window(_EGLDisplay *display,
                                        _EGLConfig *config, void *window,
                                        const EGLint *list) {
  return create_surface(display, config, window, list);
}

static _EGLSurface *plant_create_pbuffer(_EGLDisplay *display,
                                         _EGLConfig *config,
                                         const EGLint *list) {
  return create_surface(display, config, NULL, list);
}

static bool bind_context(_EGLContext *context, _EGLSurface *draw,
                         _EGLSurface *read) {
  return st_api_make_current(
      context ? ((struct plant_context *)context)->st : NULL,
      draw ? &((struct plant_surface *)draw)->drawable : NULL,
      read ? &((struct plant_surface *)read)->drawable : NULL);
}

static EGLBoolean plant_make_current(_EGLDisplay *display, _EGLSurface *draw,
                                     _EGLSurface *read, _EGLContext *context) {
  _EGLContext *old_context;
  _EGLSurface *old_draw, *old_read;
  /* EGL drops its display lock around the driver call. Serialize ownership
   * checks and rebinding, including switches between different displays. */
  simple_mtx_lock(&binding_mutex);
  if (!_eglBindContext(context, draw, read, &old_context, &old_draw,
                       &old_read)) {
    simple_mtx_unlock(&binding_mutex);
    return EGL_FALSE;
  }
  if (old_context)
    st_context_flush(((struct plant_context *)old_context)->st, ST_FLUSH_FRONT,
                     NULL, NULL, NULL);
  bool bound = bind_context(context, draw, read);
  if (!bound) {
    _EGLContext *temporary_context;
    _EGLSurface *temporary_draw, *temporary_read;
    _eglBindContext(old_context, old_draw, old_read, &temporary_context,
                    &temporary_draw, &temporary_read);
    plant_destroy_surface(display, temporary_draw);
    plant_destroy_surface(display, temporary_read);
    plant_destroy_context(display, temporary_context);
    if (!bind_context(old_context, old_draw, old_read)) {
      _eglBindContext(NULL, NULL, NULL, &temporary_context, &temporary_draw,
                      &temporary_read);
      st_api_make_current(NULL, NULL, NULL);
      plant_destroy_surface(display, temporary_draw);
      plant_destroy_surface(display, temporary_read);
      plant_destroy_context(display, temporary_context);
    }
  }
  plant_destroy_surface(display, old_draw);
  plant_destroy_surface(display, old_read);
  plant_destroy_context(display, old_context);
  simple_mtx_unlock(&binding_mutex);
  return bound ? EGL_TRUE
               : _eglError(EGL_BAD_ALLOC, "eglMakeCurrent framebuffer");
}

static EGLBoolean plant_swap_buffers(_EGLDisplay *display, _EGLSurface *base) {
  struct plant_surface *surface = (void *)base;
  struct st_context *st = ((struct plant_context *)base->CurrentContext)->st;
  struct pipe_fence_handle *fence = NULL;
  st_context_flush(st, ST_FLUSH_WAIT | ST_FLUSH_END_OF_FRAME, &fence, NULL,
                   NULL);
  bool back = surface->visual.buffer_mask & ST_ATTACHMENT_BACK_LEFT_MASK;
  if (!surface_present(st, surface,
                       back ? ST_ATTACHMENT_BACK_LEFT
                            : ST_ATTACHMENT_FRONT_LEFT))
    return _eglError(EGL_BAD_SURFACE, "GUI window presentation");
  if (back) {
    struct pipe_resource *front = surface->textures[ST_ATTACHMENT_FRONT_LEFT];
    surface->textures[ST_ATTACHMENT_FRONT_LEFT] =
        surface->textures[ST_ATTACHMENT_BACK_LEFT];
    surface->textures[ST_ATTACHMENT_BACK_LEFT] = front;
    p_atomic_inc(&surface->drawable.stamp);
    st_context_invalidate_state(st, ST_INVALIDATE_FB_STATE);
  }
  return EGL_TRUE;
}

static EGLBoolean plant_wait_client(_EGLDisplay *display,
                                    _EGLContext *context) {
  struct pipe_fence_handle *fence = NULL;
  st_context_flush(((struct plant_context *)context)->st,
                   ST_FLUSH_FRONT | ST_FLUSH_WAIT, &fence, NULL, NULL);
  return EGL_TRUE;
}

static EGLBoolean plant_wait_native(EGLint engine) {
  return engine == EGL_CORE_NATIVE_ENGINE
             ? EGL_TRUE
             : _eglError(EGL_BAD_PARAMETER, "eglWaitNative");
}

static EGLBoolean plant_copy_buffers(_EGLDisplay *display, _EGLSurface *surface,
                                     void *pixmap) {
  return _eglError(EGL_BAD_NATIVE_PIXMAP, "native pixmaps are unavailable");
}

const _EGLDriver _eglDriver = {
    .Initialize = plant_initialize,
    .Terminate = plant_terminate,
    .CreateContext = plant_create_context,
    .DestroyContext = plant_destroy_context,
    .MakeCurrent = plant_make_current,
    .CreateWindowSurface = plant_create_window,
    .CreatePbufferSurface = plant_create_pbuffer,
    .DestroySurface = plant_destroy_surface,
    .SwapBuffers = plant_swap_buffers,
    .CopyBuffers = plant_copy_buffers,
    .WaitClient = plant_wait_client,
    .WaitNative = plant_wait_native,
};
