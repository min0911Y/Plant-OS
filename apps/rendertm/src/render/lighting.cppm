module;

#include "../prelude.hpp"

export module lighting;

import math;
import execution;
import camera;
import noise;
import world;
import terrain;
import settings;
import framebuffer;

export struct ShadingContext
{
    LinearColor albedo;
    LinearColor sky_top;
    LinearColor hemi_ground;
    float sky_scale;
    Vec3 camera_pos;
    double ambient_light;
    Material material;
    bool direct_lighting_enabled;
    bool ambient_occlusion_enabled;
    std::array<DirectionalLight, 2> lights;
};

namespace {

[[nodiscard]]
auto disk_samples() -> const std::array<Vec2, 64>&
{
    static const auto samples = [] {
        std::array<Vec2, 64> points{};
        constexpr double golden_angle = 2.39996322972865332;
        constexpr double count_inv = 1.0 / static_cast<double>(points.size());

        for (size_t i = 0; i < points.size(); ++i)
        {
            const double u = (static_cast<double>(i) + 0.5) * count_inv;
            const double r = std::sqrt(u);
            const double theta = static_cast<double>(i) * golden_angle;
            points[i] = {r * std::cos(theta), r * std::sin(theta)};
        }
        return points;
    }();
    return samples;
}

}

export struct ShadowJitter
{
    Vec3 right{0.0, 0.0, 0.0};
    Vec3 up{0.0, 0.0, 0.0};
    BlueNoise::Shift shift_u{};
    BlueNoise::Shift shift_v{};

    [[nodiscard]]
    static auto for_light(const DirectionalLight& light, const uint32_t frame_index,
                          const int salt) -> ShadowJitter
    {
        ShadowJitter jitter{
            .shift_u = BlueNoise::shift(static_cast<int>(frame_index), salt),
            .shift_v = BlueNoise::shift(static_cast<int>(frame_index), salt + 1)
        };
        if (light.angular_radius > 0.0 && light.intensity > 0.0)
        {
            auto [right, up, forward] = Vec3::get_basis(light.dir);
            const double scale = std::tan(light.angular_radius);
            jitter.right = right * scale;
            jitter.up = up * scale;
        }
        return jitter;
    }

    [[nodiscard]]
    auto direction(const Vec3& light_dir, const int px, const int py) const -> Vec3
    {
        if (right.x == 0.0 && right.y == 0.0 && right.z == 0.0)
        {
            return light_dir;
        }

        const float u = BlueNoise::sample(px, py, shift_u);
        const float v = BlueNoise::sample(px, py, shift_v);

        const size_t ix = static_cast<size_t>(u * 8.0f) & 7;
        const size_t iy = static_cast<size_t>(v * 8.0f) & 7;
        const Vec2 sample = disk_samples()[(iy << 3) | ix];
        return light_dir + (right * sample.x) + (up * sample.y);
    }
};

export struct DirectFrame
{
    float depth_max = 0.0f;
    uint32_t frame_index = 0;
    bool shadows_on = false;
    std::array<DirectionalLight, 2> lights{};
};

export struct GiFrame
{
    size_t width = 0;
    size_t height = 0;
    float depth_max = 0.0f;
    uint32_t frame_index = 0;
    double jitter_x = 0.0;
    double jitter_y = 0.0;
    int bounces = 0;
    float scale = 0.0f;
    float sky_scale = 0.0f;
    LinearColor sky_top{};
    LinearColor hemi_ground{};
    std::array<DirectionalLight, 2> lights{};
    Mat4 inv_current_vp = Mat4::identity();
};

export struct LightingEngine
{
    using FloatSpan = std::span<const float>;
    using FloatSpanMut = std::span<float>;
    using VecSpan = std::span<const Vec3>;

    [[nodiscard]]
    static auto fresnel(const double vdoth, const double f0) -> double
    {
        if (f0 <= 0.0)
        {
            return 0.0;
        }
        const double f0_clamped = std::clamp(f0, 0.0, 1.0);
        const double one_minus = 1.0 - std::clamp(vdoth, 0.0, 1.0);
        const double one_minus2 = one_minus * one_minus;
        const double pow5 = one_minus2 * one_minus2 * one_minus;
        return f0_clamped + (1.0 - f0_clamped) * pow5;
    }

    [[nodiscard]]
    static auto spec_norm(const double shininess) -> double
    {
        return (shininess + 8.0) / (8.0 * std::numbers::pi_v<double>);
    }

    [[nodiscard]]
    static auto specular_term(const double ndoth, const double vdoth, const double ndotl,
                              const double shininess, const double f0) -> double
    {
        if (ndoth <= 0.0 || ndotl <= 0.0 || f0 <= 0.0)
        {
            return 0.0;
        }
        const double power = std::pow(std::clamp(ndoth, 0.0, 1.0), shininess);
        const double f = fresnel(vdoth, f0);
        return spec_norm(shininess) * power * f * std::clamp(ndotl, 0.0, 1.0);
    }

    [[nodiscard]]
    auto hemi_ground(const LinearColor& base,
                     const std::array<DirectionalLight, 2>& lights,
                     const LightingSettings& lighting) const -> LinearColor
    {
        double energy = 0.0;
        for (const auto& light : lights)
        {
            if (light.intensity > 0.0)
            {
                energy += light.intensity * std::clamp(light.dir.y, 0.0, 1.0);
            }
        }

        const double bounce = std::clamp(energy * lighting.hemisphere_bounce_strength, 0.0, 1.0);
        if (bounce <= std::numeric_limits<double>::epsilon())
        {
            return base;
        }
        return LinearColor::lerp(base, lighting.hemisphere_bounce_color,
                                 static_cast<float>(bounce));
    }

    [[nodiscard]]
    auto shadow_factor(const Terrain& terrain,
                       const Vec3& light_dir, const Vec3& world_pos, const Vec3& normal,
                       const ShadowSettings& shadow) const -> float
    {
        if (normal.dot(light_dir) <= 0.0)
        {
            return 1.0f;
        }
        const Vec3 origin = world_pos + (normal * shadow.ray_bias);
        return terrain.raycast(origin, light_dir) ? 0.0f : 1.0f;
    }

    auto filter_shadows(FloatSpan mask_a, FloatSpan mask_b,
                        FloatSpanMut out_a, FloatSpanMut out_b,
                        FloatSpan depth, VecSpan normals,
                        const size_t width, const size_t height, const float depth_max,
                        const ShadowSettings& shadow, RenderWorkers* workers = nullptr) const -> void
    {
        RenderWorkers::rows(workers, height, [&](size_t begin, size_t end) {
        for (size_t y = begin; y < end; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                const size_t center_idx = y * width + x;
                const float center_depth = depth[center_idx];

                const Vec3 center_normal = normals[center_idx];
                const bool is_background = center_depth >= depth_max;
                const bool is_invalid_normal = center_normal.dot(center_normal) <= 1e-6;

                if (is_background || is_invalid_normal)
                {
                    out_a[center_idx] = mask_a[center_idx];
                    out_b[center_idx] = mask_b[center_idx];
                    continue;
                }

                float sum_a = mask_a[center_idx] * shadow.filter_center_weight;
                float sum_b = mask_b[center_idx] * shadow.filter_center_weight;
                float weight_sum = shadow.filter_center_weight;

                auto process_neighbor = [&](size_t idx) {
                    const float neighbor_depth = depth[idx];
                    if (neighbor_depth >= depth_max) return;

                    const Vec3 neighbor_normal = normals[idx];
                    if (neighbor_normal.dot(neighbor_normal) <= 1e-6) return;

                    if (std::abs(neighbor_depth - center_depth) > shadow.filter_depth_threshold) return;

                    const float dot = static_cast<float>(center_normal.dot(neighbor_normal));
                    if (std::clamp(dot, -1.0f, 1.0f) < shadow.filter_normal_threshold) return;

                    const float w = shadow.filter_neighbor_weight;
                    sum_a += mask_a[idx] * w;
                    sum_b += mask_b[idx] * w;
                    weight_sum += w;
                };

                if (x > 0) process_neighbor(center_idx - 1);
                if (x + 1 < width) process_neighbor(center_idx + 1);
                if (y > 0) process_neighbor(center_idx - width);
                if (y + 1 < height) process_neighbor(center_idx + width);

                if (weight_sum <= std::numeric_limits<float>::epsilon())
                {
                    out_a[center_idx] = mask_a[center_idx];
                    out_b[center_idx] = mask_b[center_idx];
                }
                else
                {
                    const float inv_weight = 1.0f / weight_sum;
                    out_a[center_idx] = std::clamp(sum_a * inv_weight, 0.0f, 1.0f);
                    out_b[center_idx] = std::clamp(sum_b * inv_weight, 0.0f, 1.0f);
                }
            }
        }
        });
    }

    auto resolve_direct(const DirectFrame& frame,
                        RenderBuffers& buffers,
                        const Terrain& terrain,
                        const ShadowSettings& shadow, RenderWorkers* workers = nullptr) const -> void
    {
        const size_t samples = buffers.zbuffer.size();
        if (!frame.shadows_on)
        {
            RenderWorkers::rows(workers, samples, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            {
                buffers.sample_direct[i] = buffers.sample_direct_sun[i] + buffers.sample_direct_moon[i];
            }
        });
            return;
        }

        const auto masks = resolve_shadow_masks(frame, buffers, terrain, shadow, workers);
        RenderWorkers::rows(workers, samples, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
        {
            buffers.sample_direct[i] = buffers.sample_direct_sun[i] * masks[0][i]
                                       + buffers.sample_direct_moon[i] * masks[1][i];
        }
        });
    }

    auto gi_pass(const GiFrame& frame,
                 RenderBuffers& buffers,
                 const Terrain& terrain,
                 const GiSettings& gi, RenderWorkers* workers = nullptr) const -> void
    {
        RenderWorkers::rows(workers, frame.height, [&](size_t begin, size_t end) {
        for (size_t y = begin; y < end; ++y)
        {
            const double screen_y = static_cast<double>(y) + 0.5 + frame.jitter_y;
            for (size_t x = 0; x < frame.width; ++x)
            {
                const size_t idx = y * frame.width + x;
                if (buffers.zbuffer[idx] >= frame.depth_max)
                {
                    continue;
                }
                buffers.sample_indirect[idx] = {0.0f, 0.0f, 0.0f};
                const Vec3 normal = buffers.sample_normals[idx];
                if (normal.dot(normal) <= 1e-6)
                {
                    continue;
                }
                const LinearColor albedo = buffers.sample_albedo[idx];

                const double screen_x = static_cast<double>(x) + 0.5 + frame.jitter_x;
                const Vec3 world_pos = Camera::screen_to_world(
                    screen_x, screen_y, buffers.zbuffer[idx],
                    frame.inv_current_vp,
                    static_cast<double>(frame.width), static_cast<double>(frame.height));
                buffers.world_positions[idx] = world_pos;
                buffers.world_stamp[idx] = frame.frame_index;

                LinearColor gi_sum{0.0f, 0.0f, 0.0f};
                for (int sample_idx = 0; sample_idx < gi.sample_count; ++sample_idx)
                {
                    LinearColor gi_sample{0.0f, 0.0f, 0.0f};
                    Vec3 cur_world = world_pos;
                    Vec3 cur_normal = normal;
                    LinearColor throughput = albedo;
                    bool hit_any = false;

                    for (int bounce = 0; bounce < frame.bounces; ++bounce)
                    {
                        const int salt = gi.noise_salt
                                         + (bounce * gi.sample_count + sample_idx) * 2;
                        Vec3 dir{};
                        double cos_theta = 0.0;
                        if (!sample_dir(cur_normal, frame.frame_index, salt,
                                        static_cast<int>(x), static_cast<int>(y),
                                        dir, cos_theta))
                        {
                            break;
                        }
                        const Vec3 origin = cur_world + (cur_normal * gi.ray_bias);
                        const auto hit = terrain.raycast(origin, dir, gi.max_distance);
                        if (!hit)
                        {
                            break;
                        }

                        const LinearColor hit_albedo = terrain.albedo[hit->block->material];
                        const float hit_visibility = hit->block->face_visibility(hit->face);
                        const LinearColor incoming = eval_incoming(hit->normal, hit_visibility,
                                                                   frame.lights,
                                                                   frame.sky_top, frame.hemi_ground,
                                                                   frame.sky_scale);
                        if (incoming.r > 0.0f || incoming.g > 0.0f || incoming.b > 0.0f)
                        {
                            const LinearColor bounced =
                                incoming * hit_albedo * throughput * static_cast<float>(cos_theta);
                            gi_sample = gi_sample + bounced;
                            hit_any = true;
                        }

                        const LinearColor next_throughput =
                            throughput * hit_albedo * static_cast<float>(cos_theta);
                        if (next_throughput.r <= 0.0f && next_throughput.g <= 0.0f &&
                            next_throughput.b <= 0.0f)
                        {
                            break;
                        }

                        throughput = next_throughput;
                        cur_world = hit->position;
                        cur_normal = hit->normal;
                    }

                    if (hit_any)
                    {
                        gi_sum = gi_sum + gi_sample;
                    }
                }

                if (gi_sum.r > 0.0f || gi_sum.g > 0.0f || gi_sum.b > 0.0f)
                {
                    const float inv_samples = 1.0f / static_cast<float>(gi.sample_count);
                    LinearColor gi_sample = gi_sum * (inv_samples * frame.scale);
                    gi_sample.r = std::clamp(gi_sample.r, 0.0f, gi.clamp);
                    gi_sample.g = std::clamp(gi_sample.g, 0.0f, gi.clamp);
                    gi_sample.b = std::clamp(gi_sample.b, 0.0f, gi.clamp);
                    buffers.sample_indirect[idx] = gi_sample;
                }
            }
        }
        });
    }

private:
    static constexpr double kPi = std::numbers::pi_v<double>;

    auto resolve_shadow_masks(const DirectFrame& frame,
                              RenderBuffers& buffers,
                              const Terrain& terrain,
                              const ShadowSettings& shadow, RenderWorkers* workers) const
        -> std::array<const float*, 2>
    {
        trace_shadows(frame, buffers, terrain, shadow, workers);
        if (!shadow.filter_enabled)
        {
            return {buffers.shadow_mask_sun.data(), buffers.shadow_mask_moon.data()};
        }

        const size_t samples = buffers.zbuffer.size();
        filter_shadows({buffers.shadow_mask_sun.data(), samples},
                       {buffers.shadow_mask_moon.data(), samples},
                       {buffers.shadow_mask_filtered_sun.data(), samples},
                       {buffers.shadow_mask_filtered_moon.data(), samples},
                       {buffers.zbuffer.data(), samples},
                       {buffers.sample_normals.data(), samples},
                       buffers.width, buffers.height, frame.depth_max, shadow, workers);
        return {buffers.shadow_mask_filtered_sun.data(),
                buffers.shadow_mask_filtered_moon.data()};
    }

    auto trace_shadows(const DirectFrame& frame,
                       RenderBuffers& buffers,
                       const Terrain& terrain,
                       const ShadowSettings& shadow, RenderWorkers* workers = nullptr) const -> void
    {
        const std::array masks{
            buffers.shadow_mask_sun.data(), buffers.shadow_mask_moon.data()
        };
        const std::array direct{
            buffers.sample_direct_sun.data(), buffers.sample_direct_moon.data()
        };
        const std::array jitter{
            ShadowJitter::for_light(frame.lights[0], frame.frame_index, shadow.sun_salt),
            ShadowJitter::for_light(frame.lights[1], frame.frame_index, shadow.moon_salt)
        };

        const auto sample = [&](const size_t light, const size_t pixel,
                                const int x, const int y) -> float {
            const LinearColor& value = direct[light][pixel];
            if (value.r <= 0.0f && value.g <= 0.0f && value.b <= 0.0f) return 1.0f;
            const Vec3 direction = jitter[light].direction(frame.lights[light].dir, x, y);
            return shadow_factor(terrain, direction, buffers.world_positions[pixel],
                                 buffers.sample_normals[pixel], shadow);
        };

        RenderWorkers::rows(workers, buffers.height, [&](size_t begin, size_t end) {
        for (size_t y = begin; y < end; ++y)
        {
            for (size_t x = 0; x < buffers.width; ++x)
            {
                const size_t pixel = y * buffers.width + x;
                if (buffers.zbuffer[pixel] >= frame.depth_max) continue;
                masks[0][pixel] = sample(0, pixel, static_cast<int>(x), static_cast<int>(y));
                masks[1][pixel] = sample(1, pixel, static_cast<int>(x), static_cast<int>(y));
            }
        }
        });
    }

    static auto sincos_double(const double angle, double* out_sin, double* out_cos) -> void
    {
        *out_sin = std::sin(angle);
        *out_cos = std::cos(angle);

    }

    [[nodiscard]]
    static auto sample_dir(const Vec3& hemi_normal,
                           const uint32_t frame_index, const int salt,
                           const int px, const int py,
                           Vec3& out_dir,
                           double& out_cos) -> bool
    {
        const int frame = static_cast<int>(frame_index);
        const float u1 = BlueNoise::sample(px, py, frame, salt);
        const float u2 = BlueNoise::sample(px, py, frame, salt + 1);
        const double r = std::sqrt(static_cast<double>(u1));
        const double theta = 2.0 * kPi * static_cast<double>(u2);
        double sin_theta = 0.0;
        double cos_theta = 0.0;
        sincos_double(theta, &sin_theta, &cos_theta);
        const double local_x = r * cos_theta;
        const double local_y = r * sin_theta;
        const double local_z = std::sqrt(std::max(0.0, 1.0 - r * r));

        auto [tangent, bitangent, forward] = Vec3::get_basis(hemi_normal);
        out_dir = tangent * local_x + bitangent * local_y + forward * local_z;
        out_cos = local_z;
        return out_cos > 0.0;
    }

    [[nodiscard]]
    static auto eval_incoming(const Vec3& normal, const float sky_visibility,
                              const std::array<DirectionalLight, 2>& lights,
                              const LinearColor& sky_top,
                              const LinearColor& hemi_ground,
                              const float sky_scale) -> LinearColor
    {
        LinearColor incoming{0.0f, 0.0f, 0.0f};
        for (const auto& light : lights)
        {
            if (light.intensity <= 0.0)
            {
                continue;
            }
            const double ndotl = normal.dot(light.dir);
            if (ndotl <= 0.0)
            {
                continue;
            }
            incoming = incoming + light.color * static_cast<float>(light.intensity * ndotl);
        }

        if (sky_scale > 0.0f)
        {
            const float sky_t = std::clamp(static_cast<float>(normal.y * 0.5 + 0.5), 0.0f, 1.0f);
            const LinearColor sky = LinearColor::lerp(hemi_ground, sky_top, sky_t);
            incoming = incoming + sky * (sky_scale * sky_visibility);
        }

        return incoming;
    }
};
