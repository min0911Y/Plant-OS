#include "../rendertm/src/prelude.hpp"
#include <SDL3/SDL.h>
#include <climits>
#include <cstdlib>
#include <pthread.h>
#include <task.h>

import control;
import input;

namespace {

struct Options {
  int width = 480, height = 300;
#ifdef PLANT_ARCH_X86_64
  unsigned workers = std::max(1u, cpu_count()) - 1;
#else
  unsigned workers = 0; // i386 threads in one address space share one CPU.
#endif
  unsigned frames = 0;
  bool test = false;

  bool parse(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--test") == 0) {
        test = true;
        continue;
      }
      const std::string_view option = argv[i];
      if (++i == argc)
        return false;
      unsigned value;
      auto end = argv[i] + std::strlen(argv[i]);
      auto parsed = std::from_chars(argv[i], end, value);
      if (parsed.ec != std::errc{} || parsed.ptr != end)
        return false;
      if (option == "--workers")
        workers = value;
      else if (option == "--frames" && value)
        frames = value;
      else if (option == "--width" && value && value <= INT_MAX / 4)
        width = value;
      else if (option == "--height" && value && value <= INT_MAX)
        height = value;
      else
        return false;
    }
    return workers <= cpu_count() && !(test && frames);
  }
};

class Application {
  enum class Frame { Idle, Rendering, Ready, Stop };
  Options options;
  RenderEngine engine;
  RenderInputMailbox mailbox;
  SDL_Window *window = nullptr;
  SDL_Surface *surface = nullptr, *converted = nullptr;
  pthread_t thread = nullptr;
  std::mutex mutex;
  std::condition_variable changed;
  Frame state = Frame::Idle;
  uint64_t render_ns = 0, total_ns = 0;
  unsigned frames = 0, phase = 0;
  bool mouse_valid = false;
  float mouse_x = 0, mouse_y = 0;

  void render() {
    std::unique_lock lock(mutex);
    for (;;) {
      changed.wait(lock, [&] { return state != Frame::Idle; });
      if (state == Frame::Stop)
        return;
      if (state != Frame::Rendering) {
        changed.wait(lock, [&] { return state != Frame::Ready; });
        continue;
      }
      lock.unlock();
      mailbox.drain().apply(engine);
      uint64_t start = monotonic_ns();
      SDL_Surface *target = converted ? converted : surface;
      engine.update(static_cast<uint32_t *>(target->pixels), target->w,
                    target->h, target->pitch / sizeof(uint32_t));
      uint64_t elapsed = monotonic_ns() - start;
      lock.lock();
      render_ns = elapsed;
      if (state != Frame::Stop)
        state = Frame::Ready;
    }
  }

  void request_frame() {
    std::lock_guard lock(mutex);
    state = Frame::Rendering;
    changed.notify_one();
  }

  bool verify() {
    RenderEngine reference;
    const size_t width = surface->w, height = surface->h, pitch = width + 5;
    std::vector<uint32_t> serial(width * height),
        parallel(pitch * height, 0xdeadbeef);
    reference.settings.paused = engine.settings.paused = true;
    for (unsigned frame = 0; frame < 3; ++frame) {
      reference.settings.gi.enabled = engine.settings.gi.enabled = frame == 2;
      reference.settings.gi.strength = engine.settings.gi.strength = 1.0;
      reference.settings.ambient_occlusion_enabled =
          engine.settings.ambient_occlusion_enabled = frame != 1;
      reference.camera.rotate({0.02, 0.01});
      engine.camera.rotate({0.02, 0.01});
      uint64_t start = monotonic_ns();
      reference.update(serial.data(), width, height);
      uint64_t serial_ns = monotonic_ns() - start;
      start = monotonic_ns();
      engine.update(parallel.data(), width, height, pitch);
      uint64_t parallel_ns = monotonic_ns() - start;
      for (size_t y = 0; y < height; ++y) {
        if (std::memcmp(serial.data() + y * width, parallel.data() + y * pitch,
                        width * sizeof(uint32_t)) != 0)
          return SDL_SetError(
              "Parallel renderer differs from serial reference");
        for (size_t x = width; x < pitch; ++x)
          if (parallel[y * pitch + x] != 0xdeadbeef)
            return SDL_SetError("Renderer overwrote row padding");
      }
      logkf("RENDERHD VERIFY frame=%u width=%u height=%u workers=%u "
            "serial_ns=%llu parallel_ns=%llu\n",
            frame, (unsigned)width, (unsigned)height, options.workers,
            (unsigned long long)serial_ns, (unsigned long long)parallel_ns);
    }
    engine.settings.gi.enabled = false;
    return true;
  }

  bool present() {
    if (converted && !SDL_BlitSurface(converted, nullptr, surface, nullptr))
      return false;
    if (!SDL_UpdateWindowSurface(window))
      return false;
    ++frames;
    total_ns += render_ns;
    if (options.test) {
      uint32_t hash = 2166136261u;
      for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
          Uint8 r, g, b;
          if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr))
            return false;
          for (Uint8 byte : {r, g, b})
            hash = (hash ^ byte) * 16777619u;
        }
      }
      int x, y;
      SDL_GetWindowPosition(window, &x, &y);
      logkf("RENDERHD FRAME phase=%u x=%d y=%d width=%d height=%d hash=%08x "
            "paused=%d ao=%d gi=%d\n",
            phase, x, y, surface->w, surface->h, hash, engine.settings.paused,
            engine.settings.ambient_occlusion_enabled,
            engine.settings.gi.enabled);
    }
    char title[256];
    std::snprintf(title, sizeof(title),
                  "RenderTM HD | %dx%d | %.1f fps | Cam %.2f %.2f %.2f | Rot "
                  "%.1f %.1f | AO %s GI %s | %s",
                  surface->w, surface->h, render_ns ? 1e9 / render_ns : 0.0,
                  engine.camera.position.x, engine.camera.position.y,
                  engine.camera.position.z, engine.camera.rotation.x,
                  engine.camera.rotation.y,
                  engine.settings.ambient_occlusion_enabled ? "on" : "off",
                  engine.settings.gi.enabled ? "on" : "off",
                  engine.settings.paused ? "paused" : "running");
    return SDL_SetWindowTitle(window, title);
  }

public:
  explicit Application(Options options) : options(options) {}
  ~Application() {
    {
      std::lock_guard lock(mutex);
      state = Frame::Stop;
    }
    changed.notify_one();
    if (thread)
      pthread_join(thread, nullptr);
    SDL_DestroySurface(converted);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }

  int run() {
    if (!SDL_Init(SDL_INIT_VIDEO))
      return 1;
    const SDL_DisplayMode *display =
        SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (!display)
      return 1;
    int width = std::min(options.width, display->w - 8);
    int height = std::min(options.height, display->h - 48);
    if (width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / height / sizeof(Vec3))
      return SDL_SetError("Invalid render dimensions"), 1;
    if (!engine.workers.start(options.workers))
      return SDL_SetError("Cannot create render workers"), 1;
    window = SDL_CreateWindow("RenderTM HD", width, height, 0);
    if (!window || !(surface = SDL_GetWindowSurface(window)))
      return 1;
    if ((surface->format != SDL_PIXELFORMAT_ARGB8888 &&
         surface->format != SDL_PIXELFORMAT_XRGB8888) ||
        surface->pitch % sizeof(uint32_t) != 0) {
      converted = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
      if (!converted)
        return 1;
    }
    if (options.test && !verify())
      return 1;
    if (options.frames)
      engine.settings.paused = true;
    int created = pthread_create(
        &thread, nullptr,
        [](void *self) -> void * {
          static_cast<Application *>(self)->render();
          return nullptr;
        },
        this);
    if (created != 0)
      return SDL_SetError("Cannot create rendering thread"), 1;
    request_frame();
    bool running = true, dirty = false;
    Uint64 last_input = SDL_GetTicks(), deadline = last_input + 60000;
    while (running) {
      SDL_Event event;
      bool motion = false;
      if (SDL_WaitEventTimeout(&event, 8)) {
        do {
          if (event.type == SDL_EVENT_QUIT ||
              event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            running = false;
          if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
            mouse_valid = false;
          if (event.type == SDL_EVENT_MOUSE_MOTION) {
            mouse_x = event.motion.x;
            mouse_y = event.motion.y;
            mouse_valid = true;
            motion = true;
          }
          if (event.type != SDL_EVENT_KEY_DOWN)
            continue;
          InputAction action = event.key.key <= 127
                                   ? InputParser::key_to_action(event.key.key)
                                   : InputAction::None;
          switch (event.key.key) {
          case SDLK_UP:
            action = InputAction::MoveForward;
            break;
          case SDLK_DOWN:
            action = InputAction::MoveBackward;
            break;
          case SDLK_LEFT:
            action = InputAction::MoveLeft;
            break;
          case SDLK_RIGHT:
            action = InputAction::MoveRight;
            break;
          case SDLK_ESCAPE:
            action = InputAction::Quit;
            break;
          }
          switch (action) {
          case InputAction::Quit:
            running = false;
            break;
          case InputAction::TogglePause:
            if (!event.key.repeat)
              mailbox.push_toggle_pause();
            break;
          case InputAction::ToggleGI:
            if (!event.key.repeat)
              mailbox.push_toggle_gi();
            break;
          case InputAction::ToggleAO:
            if (!event.key.repeat)
              mailbox.push_toggle_ao();
            break;
          default:
            mailbox.push_move(InputParser::movement_intent(action) * 0.2);
            break;
          }
          if (options.test &&
              ((phase == 0 && action == InputAction::MoveForward) ||
               (phase == 2 && action == InputAction::ToggleAO) ||
               (phase == 3 && action == InputAction::ToggleGI) ||
               (phase == 4 && action == InputAction::TogglePause)))
            ++phase;
          dirty |= action != InputAction::None;
        } while (SDL_PollEvent(&event));
      }
      if (!running)
        break;
      Uint64 now = SDL_GetTicks();
      double dt = std::min(0.1, (now - last_input) / 1000.0);
      last_input = now;
      if (mouse_valid && (!options.test || (phase == 1 && motion))) {
        auto velocity = InputParser::mouse_look_velocity(
            {(int)mouse_x + 1, (int)mouse_y + 1, width, height,
             std::min(width, height) / 10, 1.2});
        mailbox.push_rotate({velocity.yaw * dt, velocity.pitch * dt});
        if (options.test && dt > 0 &&
            (velocity.yaw != 0 || velocity.pitch != 0)) {
          phase = 2;
          dirty = true;
        }
      }
      bool ready;
      {
        std::lock_guard lock(mutex);
        ready = state == Frame::Ready;
      }
      if (ready) {
        if (!present())
          return 1;
        {
          std::lock_guard lock(mutex);
          state = Frame::Idle;
        }
        changed.notify_one();
        deadline = now + 60000;
        if (options.frames && frames >= options.frames)
          running = false;
        if (running && (!options.test || dirty)) {
          request_frame();
          dirty = false;
        }
      } else if (dirty) {
        std::lock_guard lock(mutex);
        if (state == Frame::Idle) {
          state = Frame::Rendering;
          changed.notify_one();
          dirty = false;
        }
      }
      if (options.test && now > deadline)
        return SDL_SetError("Input regression timed out"), 1;
    }
    if (options.frames)
      logkf("RENDERHD BENCH width=%d height=%d workers=%u frames=%u "
            "render_ns=%llu\n",
            width, height, options.workers, frames,
            (unsigned long long)total_ns);
    if (options.test) {
      if (phase != 5)
        return SDL_SetError("Incomplete input regression"), 1;
      logkf("RENDERHD PASS pixels keyboard mouse AO GI pause\n");
    }
    return 0;
  }
};

} // namespace

extern "C" int main(int argc, char **argv) {
  Options options;
  if (!options.parse(argc, argv)) {
    std::puts("renderhd.bin [--width N] [--height N] [--workers N] [--frames "
              "N] [--test]\n"
              "WASD/arrows move; R/F rise/fall; mouse look; P pause; G GI; O "
              "AO; Q/Escape quit.");
    return argc == 2 && std::strcmp(argv[1], "--help") == 0 ? 0 : 1;
  }
  Application app(options);
  int status = app.run();
  if (status) {
    std::fprintf(stderr, "RenderTM HD: %s\n", SDL_GetError());
    if (options.test)
      logkf("RENDERHD FAIL %s\n", SDL_GetError());
  }
  return status;
}
