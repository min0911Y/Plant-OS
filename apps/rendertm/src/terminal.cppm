module;

#include "prelude.hpp"

export module terminal;

import render;
export import control;

export struct TerminalSize
{
    size_t width = 0;
    size_t height = 0;
};

export struct TerminalRender
{
    static void init();
    static void shutdown();
    static void update_size();
    static void submit_frame(RenderEngine& engine, RenderInputMailbox& mailbox);
    static void output_loop(std::stop_token token);
    static auto size() -> TerminalSize;
};

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kRenderChar = "▀";
constexpr std::string_view kEnterAlt = "\033[?1049h\033[?25l\033[?1003h\033[?1006h";
constexpr std::string_view kLeaveAlt = "\033[?1003l\033[?1006l\033[?25h\033[?1049l";

std::atomic_size_t raw_width{0};
std::atomic_size_t raw_height{0};
std::atomic<double> output_fps{0.0};

struct RenderFrame {
    size_t width = 0;
    size_t height = 0;
    double render_fps = 0.0;
    Vec3 cam_pos{};
    Vec2 cam_rot{};
    double sharpen = 0.0;
    double sharpen_pct = 0.0;
    std::vector<uint32_t> pixels;
};

struct FpsCounter {
    Clock::time_point last = Clock::now();
    int frame_count = 0;
    double fps = 0.0;

    double tick() {
        ++frame_count;
        const auto now = Clock::now();
        const std::chrono::duration<double> elapsed = now - last;
        if (elapsed.count() >= 1.0) {
            fps = frame_count / elapsed.count();
            frame_count = 0;
            last = now;
        }
        return fps;
    }
};

struct FrameQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<RenderFrame> queued;
    std::vector<uint32_t> recycled;
    bool shutdown = false;

    void submit(RenderFrame frame) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (queued && !queued->pixels.empty()) {
                if (recycled.capacity() < queued->pixels.capacity()) {
                    recycled = std::move(queued->pixels);
                } else {
                    queued->pixels.clear();
                }
            }
            queued = std::move(frame);
        }
        cv.notify_one();
    }

    bool wait(RenderFrame& out) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return queued.has_value() || shutdown; });
        if (shutdown) return false;
        out = std::move(*queued);
        queued.reset();
        return true;
    }

    std::vector<uint32_t> take_recycled() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::move(recycled);
    }

    void recycle(std::vector<uint32_t> pixels) {
        if (pixels.empty()) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (recycled.capacity() < pixels.capacity()) {
            recycled = std::move(pixels);
        }
    }

    void request_stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown = true;
        }
        cv.notify_all();
    }
};

FrameQueue frame_queue;
FpsCounter render_fps_counter;
FpsCounter output_fps_counter;

void render_format_frame(std::string& frame_buffer,
                         const RenderFrame& frame, const RenderFrame* prev) {
    const size_t width = frame.width;
    const size_t height = frame.height;
    const size_t display_rows = height / 2;
    const size_t term_height = display_rows + 1;

    frame_buffer.reserve(width * display_rows * 20);

    frame_buffer += "\033[0m\033[H\033[7m";
    const double rad_to_deg = 180.0 / std::numbers::pi_v<double>;
    const double yaw_deg = frame.cam_rot.x * rad_to_deg;
    const double pitch_deg = frame.cam_rot.y * rad_to_deg;
    const double out_fps = output_fps.load(std::memory_order_relaxed);
    const size_t info_start = frame_buffer.size();
    frame_buffer.resize(info_start + width);
    const int len = std::snprintf(frame_buffer.data() + info_start, width + 1,
                   " RenderTM v0.0.1 | Terminal:%zux%zu Pixel:%zux%zu | "
                   "Render:%.2f Output:%.2f | Sharpen:%.3f (%.0f%%) | "
                   "Cam(%.2f,%.2f,%.2f) Rot(%.1f,%.1f)",
                   width, term_height, width, height, frame.render_fps, out_fps,
                   frame.sharpen, frame.sharpen_pct, frame.cam_pos.x, frame.cam_pos.y,
                   frame.cam_pos.z, yaw_deg, pitch_deg);
    if (len >= 0 && static_cast<size_t>(len) < width)
        std::fill(frame_buffer.begin() + info_start + len, frame_buffer.end(), ' ');
    frame_buffer += "\033[0m";

    const bool have_prev = prev && prev->width == width && prev->height == height &&
                           prev->pixels.size() == frame.pixels.size();
    uint32_t last_fg = 0xFFFFFFFF;
    uint32_t last_bg = 0xFFFFFFFF;

    auto append_ansi_rgb = [&](bool fg, uint32_t color) {
        char buf[40];
        char* it = buf;
        char* const buf_end = buf + sizeof(buf) - 1;
        *it++ = '\033';
        *it++ = '[';
        *it++ = fg ? '3' : '4';
        *it++ = '8';
        *it++ = ';';
        *it++ = '2';
        *it++ = ';';

        auto append_u8 = [&](uint32_t v, char end) {
            auto result = std::to_chars(it, buf_end, v);
            it = result.ptr;
            *it++ = end;
        };

        append_u8((color >> 16) & 0xFF, ';');
        append_u8((color >> 8) & 0xFF, ';');
        auto result = std::to_chars(it, buf_end, color & 0xFF);
        it = result.ptr;
        *it++ = 'm';

        frame_buffer.append(buf, static_cast<size_t>(it - buf));
    };

    auto append_cursor = [&](size_t row, size_t col) {
        frame_buffer += "\033[";
        frame_buffer += std::to_string(row);
        frame_buffer += ";";
        frame_buffer += std::to_string(col);
        frame_buffer += "H";
    };

    auto append_run = [&](size_t y, size_t run_start, size_t run_end) {
        append_cursor(y / 2 + 2, run_start + 1);
        last_fg = 0xFFFFFFFF;
        last_bg = 0xFFFFFFFF;
        for (size_t x = run_start; x < run_end; ++x) {
            const size_t idx = y * width + x;
            const uint32_t top = frame.pixels[idx];
            const uint32_t bot = frame.pixels[idx + width];
            if (last_fg != top || last_bg != bot) {
                append_ansi_rgb(true, top);
                append_ansi_rgb(false, bot);
                last_fg = top;
                last_bg = bot;
            }
            frame_buffer += kRenderChar;
        }
    };

    if (!have_prev) {
        for (size_t y = 0; y < height; y += 2) {
            append_run(y, 0, width);
        }
        frame_buffer += "\033[0m";
        return;
    }

    for (size_t y = 0; y < height; y += 2) {
        size_t run_start = 0;
        bool in_run = false;

        for (size_t x = 0; x < width; ++x) {
            const size_t idx = y * width + x;
            const bool changed = frame.pixels[idx] != prev->pixels[idx] ||
                                 frame.pixels[idx + width] != prev->pixels[idx + width];

            if (changed) {
                if (!in_run) {
                    in_run = true;
                    run_start = x;
                }
            } else if (in_run) {
                append_run(y, run_start, x);
                in_run = false;
            }
        }

        if (in_run) {
            append_run(y, run_start, width);
        }
    }
    frame_buffer += "\033[0m";
}

}

void TerminalRender::init() {
    update_size();
    print(kEnterAlt.data());
}

void TerminalRender::submit_frame(RenderEngine& engine, RenderInputMailbox& mailbox) {
    const size_t term_width = raw_width.load(std::memory_order_relaxed);
    const size_t term_height = raw_height.load(std::memory_order_relaxed);
    if (term_width == 0 || term_height < 2) return;

    const size_t display_rows = term_height - 1;
    const size_t width = term_width;
    const size_t height = display_rows * 2;

    const double render_fps = render_fps_counter.tick();

    const RenderInput input = mailbox.drain();
    input.apply(engine);
    if (PlatformTerminal::testing &&
        (input.move.x != 0 || input.move.y != 0 || input.move.z != 0 ||
         input.rotate.x != 0 || input.rotate.y != 0 || input.toggle_pause ||
         input.toggle_ao || input.toggle_gi))
        logkf("RENDERTM STATE move=%d look=%d paused=%d ao=%d gi=%d\n",
              input.move.x != 0 || input.move.y != 0 || input.move.z != 0,
              input.rotate.x != 0 || input.rotate.y != 0, engine.settings.paused,
              engine.settings.ambient_occlusion_enabled, engine.settings.gi.enabled);

    std::vector<uint32_t> framebuffer = frame_queue.take_recycled();
    if (framebuffer.size() != width * height) framebuffer.resize(width * height);
    engine.update(framebuffer.data(), width, height);

    RenderFrame frame;
    frame.width = width;
    frame.height = height;
    frame.render_fps = render_fps;
    frame.cam_pos = engine.camera.position;
    frame.cam_rot = engine.camera.rotation;
    frame.sharpen = engine.post.sharpen_strength;
    frame.sharpen_pct = engine.post.sharpen_percent();
    frame.pixels = std::move(framebuffer);
    frame_queue.submit(std::move(frame));
}

void TerminalRender::shutdown() {
    print(kLeaveAlt.data());
}

auto TerminalRender::size() -> TerminalSize {
    return {
        raw_width.load(std::memory_order_relaxed),
        raw_height.load(std::memory_order_relaxed)
    };
}

void TerminalRender::update_size() {
    const int width = tty_get_xsize();
    const int height = tty_get_ysize();
    if (width > 0 && height > 1) {
        raw_width.store(width, std::memory_order_relaxed);
        raw_height.store(height, std::memory_order_relaxed);
    }
}

void TerminalRender::output_loop(std::stop_token token) {
    std::stop_callback stop_cb(token, [] { frame_queue.request_stop(); });
    RenderFrame frame;
    RenderFrame last_frame;
    std::string format_buffer;
    bool have_last = false;
    while (frame_queue.wait(frame)) {
        format_buffer.clear();
        render_format_frame(format_buffer, frame, have_last ? &last_frame : nullptr);
        print(format_buffer.c_str());
        output_fps.store(output_fps_counter.tick(), std::memory_order_relaxed);
        if (PlatformTerminal::testing && !have_last)
            logkf("RENDERTM OUTPUT\n");
        if (have_last) {
            std::vector<uint32_t> recycle = std::move(last_frame.pixels);
            last_frame = std::move(frame);
            frame_queue.recycle(std::move(recycle));
        } else {
            last_frame = std::move(frame);
            have_last = true;
        }
    }
}
