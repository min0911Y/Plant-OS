module;

#include "../prelude.hpp"

export module post;

import math;
import execution;
import world;
import settings;
import framebuffer;
import camera;

export struct PostFrame
{
    const FrameLighting& lighting;
    bool taa_on = false;
    bool clamp_history = false;
    float taa_factor = 0.0f;
    bool gi_active = false;
    uint32_t frame_index = 0;
    float jitter_x = 0.0f;
    float jitter_y = 0.0f;
    float exposure = 1.0f;
    Vec3 camera_pos{0.0, 0.0, 0.0};
};

export struct PostProcessor
{
    std::array<std::vector<LinearColor>, 2> taa_history;
    std::vector<LinearColor> taa_resolved;
    std::vector<LinearColor> current_linear_buffer;
    std::vector<uint8_t> taa_history_mask;
    int taa_ping_pong = 0;
    size_t taa_width = 0;
    size_t taa_height = 0;
    bool taa_history_valid = false;
    bool taa_was_enabled = false;
    Vec3 last_camera_pos{0.0, 0.0, 0.0};
    double last_camera_yaw = 0.0;
    double last_camera_pitch = 0.0;
    bool last_camera_valid = false;
    double sharpen_strength = 0.0;
    float taa_motion_activity = 0.0f;
    Mat4 currentVP = Mat4::identity();
    Mat4 previousVP = Mat4::identity();
    Mat4 inverseCurrentVP = Mat4::identity();

    static constexpr double kTaaSharpenMax = 0.25;
    static constexpr double kTaaSharpenRotThreshold = 0.25;
    static constexpr double kTaaSharpenMoveThreshold = 0.5;
    static constexpr double kTaaSharpenMoveGain = 10.0;
    static constexpr double kTaaSharpenRotGain = 20.0;
    static constexpr float kTaaSharpenAttack = 0.5f;
    static constexpr float kTaaSharpenRelease = 0.2f;

    auto resize_buffers(const size_t sample_count) -> void
    {
        taa_resolved.assign(sample_count, {0.0f, 0.0f, 0.0f});
        current_linear_buffer.assign(sample_count, {0.0f, 0.0f, 0.0f});
        taa_history_mask.assign(sample_count, 0);
    }

    auto update_taa_state(bool taa_on, size_t width, size_t height, size_t sample_count,
                          const Vec3& camera_pos, double yaw, double pitch) -> void
    {
        if (taa_on)
        {
            if (width != taa_width || height != taa_height || !taa_was_enabled)
            {
                taa_width = width;
                taa_height = height;
                taa_history[0].assign(sample_count, {0.0f, 0.0f, 0.0f});
                taa_history[1].assign(sample_count, {0.0f, 0.0f, 0.0f});
                taa_history_valid = false;
                taa_ping_pong = 0;
            }
        }
        else
        {
            taa_history_valid = false;
        }
        taa_was_enabled = taa_on;

        float sharpen = 0.0f;
        if (!taa_on)
        {
            taa_motion_activity = 0.0f;
            last_camera_valid = false;
        }
        else
        {
            double motion = 0.0;
            if (last_camera_valid)
            {
                const double dx = camera_pos.x - last_camera_pos.x;
                const double dy = camera_pos.y - last_camera_pos.y;
                const double dz = camera_pos.z - last_camera_pos.z;
                const double move = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double dyaw = yaw - last_camera_yaw;
                const double dpitch = pitch - last_camera_pitch;
                const double rot = std::sqrt(dyaw * dyaw + dpitch * dpitch);
                const double move_gain = kTaaSharpenMoveThreshold > 0.0
                    ? (move / kTaaSharpenMoveThreshold) * kTaaSharpenMoveGain
                    : 0.0;
                const double rot_gain = kTaaSharpenRotThreshold > 0.0
                    ? (rot / kTaaSharpenRotThreshold) * kTaaSharpenRotGain
                    : 0.0;
                motion = std::sqrt(move_gain * move_gain + rot_gain * rot_gain);
                motion = std::clamp(motion, 0.0, 1.0);
            }

            const float target = static_cast<float>(motion);
            if (!last_camera_valid)
            {
                taa_motion_activity = target;
            }
            else
            {
                const float blend = (target > taa_motion_activity) ? kTaaSharpenAttack : kTaaSharpenRelease;
                taa_motion_activity = taa_motion_activity + (target - taa_motion_activity) * blend;
            }

            sharpen = static_cast<float>(taa_motion_activity * kTaaSharpenMax);
        }

        last_camera_pos = camera_pos;
        last_camera_yaw = yaw;
        last_camera_pitch = pitch;
        last_camera_valid = true;
        sharpen_strength = static_cast<double>(sharpen);
    }

    [[nodiscard]]
    auto sharpen_percent() const -> double
    {
        const double max_strength = kTaaSharpenMax;
        if (max_strength <= 0.0)
        {
            return 0.0;
        }
        const double ratio = std::clamp(sharpen_strength / max_strength, 0.0, 1.0);
        return ratio * 100.0;
    }

    [[nodiscard]]
    static auto tonemap_reinhard(const LinearColor& color, float exposure_factor) -> LinearColor
    {
        if (exposure_factor <= 0.0f)
        {
            return {0.0f, 0.0f, 0.0f};
        }
        const float r = color.r * exposure_factor;
        const float g = color.g * exposure_factor;
        const float b = color.b * exposure_factor;
        return {
            tonemap_channel(r),
            tonemap_channel(g),
            tonemap_channel(b)
        };
    }

    auto resolve_frame(uint32_t* framebuffer, const RenderBuffers& buffers,
                       const PostFrame& frame, const Skybox& skybox,
                       const GiSettings& gi_settings, RenderWorkers* workers = nullptr,
                       size_t pitch = 0) -> void
    {
        static constexpr std::array<std::array<int, 4>, 4> bayer4 = {{
            {{0, 8, 2, 10}},
            {{12, 4, 14, 6}},
            {{3, 11, 1, 9}},
            {{15, 7, 13, 5}}
        }};
        const size_t width = buffers.width;
        const size_t height = buffers.height;
        const size_t sample_count = width * height;
        if (pitch == 0) pitch = width;
        const float depth_max = std::numeric_limits<float>::max();
        const double width_d = static_cast<double>(width);
        const double height_d = static_cast<double>(height);
        const double jitter_x_d = static_cast<double>(frame.jitter_x);
        const double jitter_y_d = static_cast<double>(frame.jitter_y);
        const Mat4& inv_vp = inverseCurrentVP;
        const Mat4& prev_vp = previousVP;
        const FrameLighting& lighting = frame.lighting;

        const float dither_strength = 2.0f;
        const float dither_scale = dither_strength / 16.0f;
        const bool use_history = frame.taa_on && taa_history_valid;
        const float sharpen = static_cast<float>(sharpen_strength);

        const int read_idx = (taa_ping_pong + 1) % 2;
        const int write_idx = taa_ping_pong;
        const LinearColor* history_read_ptr = taa_history[read_idx].data();
        LinearColor* history_write_ptr = taa_history[write_idx].data();
        const std::span<const LinearColor> history_read_span(history_read_ptr, sample_count);

        const bool shade_sky = lighting.star_visibility > 0.0f ||
            std::any_of(lighting.lights.begin(), lighting.lights.end(),
                        [](const DirectionalLight& light) {
                            return light.disk.radius > 0.0 && light.disk.radiance > 0.0;
                        });

        RenderWorkers::rows(workers, height, [&](size_t begin, size_t end) {
        for (size_t y = begin; y < end; ++y)
        {
            const float sky_t = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
            const LinearColor sky_row = LinearColor::lerp(lighting.sky_zenith, lighting.sky_horizon, sky_t);
            for (size_t x = 0; x < width; ++x)
            {
                const size_t idx = y * width + x;
                if (buffers.zbuffer[idx] >= depth_max)
                {
                    LinearColor sky = sky_row;
                    if (shade_sky)
                    {
                        const Vec3 point = Camera::screen_to_world(
                            static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5,
                            1.0, inv_vp, width_d, height_d);
                        const Vec3 view_dir = (point - frame.camera_pos).normalize();
                        sky = skybox.shade(sky, view_dir, lighting);
                    }
                    current_linear_buffer[idx] = sky;
                    continue;
                }
                LinearColor accum = buffers.sample_colors[idx];
                const LinearColor direct = buffers.sample_direct[idx];
                accum.r += direct.r;
                accum.g += direct.g;
                accum.b += direct.b;
                if (frame.gi_active)
                {
                    const LinearColor indirect = buffers.sample_indirect[idx];
                    const float gi_ao = std::min(1.0f, buffers.sample_ao[idx] + gi_settings.ao_lift);
                    const LinearColor indirect_scaled = indirect * gi_ao;
                    accum.r += indirect_scaled.r;
                    accum.g += indirect_scaled.g;
                    accum.b += indirect_scaled.b;
                }
                current_linear_buffer[idx] = accum;
            }
        }
        });

        const std::span<const LinearColor> current_span(current_linear_buffer);

        RenderWorkers::rows(workers, height, [&](size_t begin, size_t end) {
        for (size_t y = begin; y < end; ++y)
        {
            const double screen_y = static_cast<double>(y) + 0.5 + jitter_y_d;
            for (size_t x = 0; x < width; ++x)
            {
                const size_t pixel = y * width + x;
                const float depth = buffers.zbuffer[pixel];
                const bool is_sky = depth >= depth_max;
                const LinearColor current_linear = current_linear_buffer[pixel];

                LinearColor blended = current_linear;
                bool history_used = false;
                if (frame.taa_on)
                {
                    bool history_valid = use_history;
                    LinearColor prev = current_linear;
                    if (history_valid)
                    {
                        prev = history_read_ptr[pixel];
                        if (!is_sky)
                        {
                            const double screen_x = static_cast<double>(x) + 0.5 + jitter_x_d;
                            Vec3 world;
                            if (buffers.world_stamp[pixel] == frame.frame_index)
                            {
                                world = buffers.world_positions[pixel];
                            }
                            else
                            {
                                world = Camera::screen_to_world(screen_x, screen_y, depth,
                                                                inv_vp, width_d, height_d);
                            }

                            Vec2 prev_screen = Camera::world_to_screen(prev_vp, world, width, height);

                            if (std::isfinite(prev_screen.x) && std::isfinite(prev_screen.y))
                            {
                                prev_screen.x -= static_cast<double>(frame.jitter_x);
                                prev_screen.y -= static_cast<double>(frame.jitter_y);
                            }
                            if (std::isfinite(prev_screen.x) && std::isfinite(prev_screen.y) &&
                                prev_screen.x >= 0.0 && prev_screen.x <= static_cast<double>(width) &&
                                prev_screen.y >= 0.0 && prev_screen.y <= static_cast<double>(height))
                            {
                                prev = sample_bilinear_history(history_read_span, width, height,
                                                               prev_screen.x, prev_screen.y);
                            }
                            else
                            {
                                history_valid = false;
                            }
                        }
                    }

                    if (history_valid && frame.clamp_history)
                    {
                        LinearColor minc = current_linear;
                        LinearColor maxc = current_linear;
                        for (int ny = -1; ny <= 1; ++ny)
                        {
                            for (int nx = -1; nx <= 1; ++nx)
                            {
                                const LinearColor neighbor = sample_clamped(current_span, width, height,
                                                                            static_cast<int>(x) + nx,
                                                                            static_cast<int>(y) + ny);
                                minc.r = std::min(minc.r, neighbor.r);
                                minc.g = std::min(minc.g, neighbor.g);
                                minc.b = std::min(minc.b, neighbor.b);
                                maxc.r = std::max(maxc.r, neighbor.r);
                                maxc.g = std::max(maxc.g, neighbor.g);
                                maxc.b = std::max(maxc.b, neighbor.b);
                            }
                        }
                        prev.r = std::clamp(prev.r, minc.r, maxc.r);
                        prev.g = std::clamp(prev.g, minc.g, maxc.g);
                        prev.b = std::clamp(prev.b, minc.b, maxc.b);
                    }

                    if (history_valid)
                    {
                        blended.r = prev.r + (current_linear.r - prev.r) * frame.taa_factor;
                        blended.g = prev.g + (current_linear.g - prev.g) * frame.taa_factor;
                        blended.b = prev.b + (current_linear.b - prev.b) * frame.taa_factor;
                        history_used = true;
                    }
                    else
                    {
                        blended = current_linear;
                    }

                    history_write_ptr[pixel] = blended;
                }

                taa_resolved[pixel] = blended;
                taa_history_mask[pixel] = static_cast<uint8_t>(history_used && !is_sky);
            }
        }
        });

        const std::span<const LinearColor> resolved_span(taa_resolved);

        const bool apply_sharpen = sharpen > 0.0f;

        RenderWorkers::rows(workers, height, [&](size_t begin, size_t end) {
        for (size_t y = begin; y < end; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                const size_t pixel = y * width + x;
                const bool is_sky = buffers.zbuffer[pixel] >= depth_max;
                LinearColor resolved = taa_resolved[pixel];

                if (apply_sharpen && taa_history_mask[pixel])
                {
                    const LinearColor center = resolved;
                    const LinearColor north = sample_clamped(resolved_span, width, height,
                                                             static_cast<int>(x), static_cast<int>(y) - 1);
                    const LinearColor south = sample_clamped(resolved_span, width, height,
                                                             static_cast<int>(x), static_cast<int>(y) + 1);
                    const LinearColor west = sample_clamped(resolved_span, width, height,
                                                            static_cast<int>(x) - 1, static_cast<int>(y));
                    const LinearColor east = sample_clamped(resolved_span, width, height,
                                                            static_cast<int>(x) + 1, static_cast<int>(y));
                    const float inv = 1.0f / 8.0f;
                    const LinearColor blur{
                        (center.r * 4.0f + north.r + south.r + west.r + east.r) * inv,
                        (center.g * 4.0f + north.g + south.g + west.g + east.g) * inv,
                        (center.b * 4.0f + north.b + south.b + west.b + east.b) * inv
                    };
                    resolved.r = std::max(0.0f, center.r + (center.r - blur.r) * sharpen);
                    resolved.g = std::max(0.0f, center.g + (center.g - blur.g) * sharpen);
                    resolved.b = std::max(0.0f, center.b + (center.b - blur.b) * sharpen);
                }

                const LinearColor mapped = tonemap_reinhard(resolved, frame.exposure);
                ColorSrgb srgb = mapped.to_srgb();
                if (!is_sky)
                {
                    const float dither = (static_cast<float>(bayer4[y & 3][x & 3]) - 7.5f) * dither_scale;
                    srgb.r += dither;
                    srgb.g += dither;
                    srgb.b += dither;
                }

                framebuffer[y * pitch + x] = pack_color(srgb);
            }
        }
        });

        if (frame.taa_on)
        {
            taa_history_valid = true;
            taa_ping_pong = (taa_ping_pong + 1) % 2;
        }
    }

private:
    [[nodiscard]]
    static auto tonemap_channel(const float value) -> float
    {
        if (value <= 0.0f)
        {
            return 0.0f;
        }
        return value / (1.0f + value);
    }

    [[nodiscard]]
    static auto pack_color(const ColorSrgb& color) -> uint32_t
    {
        auto clamp_channel = [](float value) -> uint32_t {
            return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
        };
        const uint32_t r = clamp_channel(color.r);
        const uint32_t g = clamp_channel(color.g);
        const uint32_t b = clamp_channel(color.b);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    [[nodiscard]]
    static auto sample_clamped(std::span<const LinearColor> buffer,
                               size_t width, size_t height,
                               int ix, int iy) -> LinearColor
    {
        if (buffer.empty() || width == 0 || height == 0)
        {
            return {0.0f, 0.0f, 0.0f};
        }
        ix = std::clamp(ix, 0, static_cast<int>(width) - 1);
        iy = std::clamp(iy, 0, static_cast<int>(height) - 1);
        const size_t idx = static_cast<size_t>(iy) * width + static_cast<size_t>(ix);
        return buffer[idx];
    }

    [[nodiscard]]
    static auto sample_bilinear_history(std::span<const LinearColor> buffer,
                                        const size_t width, const size_t height,
                                        const double screen_x, const double screen_y) -> LinearColor
    {
        if (buffer.empty() || width == 0 || height == 0)
        {
            return {0.0f, 0.0f, 0.0f};
        }

        double x = screen_x - 0.5;
        double y = screen_y - 0.5;
        x = std::clamp(x, 0.0, static_cast<double>(width - 1));
        y = std::clamp(y, 0.0, static_cast<double>(height - 1));

        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min(x0 + 1, static_cast<int>(width) - 1);
        const int y1 = std::min(y0 + 1, static_cast<int>(height) - 1);
        const float fx = static_cast<float>(x - static_cast<double>(x0));
        const float fy = static_cast<float>(y - static_cast<double>(y0));

        const size_t row0 = static_cast<size_t>(y0) * width;
        const size_t row1 = static_cast<size_t>(y1) * width;
        const LinearColor c00 = buffer[row0 + static_cast<size_t>(x0)];
        const LinearColor c10 = buffer[row0 + static_cast<size_t>(x1)];
        const LinearColor c01 = buffer[row1 + static_cast<size_t>(x0)];
        const LinearColor c11 = buffer[row1 + static_cast<size_t>(x1)];
        const LinearColor top = LinearColor::lerp(c00, c10, fx);
        const LinearColor bottom = LinearColor::lerp(c01, c11, fx);
        return LinearColor::lerp(top, bottom, fy);
    }

};
