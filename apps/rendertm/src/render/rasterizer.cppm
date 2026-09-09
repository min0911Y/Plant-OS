module;

#include "../prelude.hpp"

export module rasterizer;

import math;
import camera;
import terrain;
import lighting;

export struct ClipVertex
{
    Vec3 view;
    Vec3 world;
    float sky_visibility;
};

export struct ScreenVertex
{
    float x, y, z;
};

export struct RasterTarget
{
    float* zbuffer;
    LinearColor* sample_colors;
    LinearColor* sample_direct_sun;
    LinearColor* sample_direct_moon;
    Vec3* sample_normals;
    LinearColor* sample_albedo;
    float* sample_ao;
    Vec3* world_positions;
    uint32_t* world_stamp;
    uint32_t frame_index;
    size_t width;
    size_t height;
};

export struct RasterInputs
{
    const ShadingContext& ctx;
    float jitter_x;
    float jitter_y;
};

export struct RasterQuadInput
{
    const RenderQuad& quad;
    double proj_scale_x;
    double proj_scale_y;
    Vec3 camera_pos;
    YawPitch orientation;
};

struct RasterTriangleInput
{
    ScreenVertex v0;
    ScreenVertex v1;
    ScreenVertex v2;
    Vec3 wp0;
    Vec3 wp1;
    Vec3 wp2;
    Vec3 normal;
    float vis0;
    float vis1;
    float vis2;
};

export struct Rasterizer
{
    auto clip_to_near(double near_plane,
                      std::span<const ClipVertex> input,
                      std::span<ClipVertex> output) const -> size_t
    {
        if (input.empty() || output.empty())
        {
            return 0;
        }
        size_t out_count = 0;
        ClipVertex prev = input[input.size() - 1];
        bool prev_inside = prev.view.z >= near_plane;

        for (size_t i = 0; i < input.size(); ++i)
        {
            const ClipVertex cur = input[i];
            const bool cur_inside = cur.view.z >= near_plane;

            if (cur_inside)
            {
                if (!prev_inside)
                {
                    const double t = (near_plane - prev.view.z) / (cur.view.z - prev.view.z);
                    if (out_count < output.size())
                    {
                        output[out_count++] = lerp_clip(prev, cur, t);
                    }
                }
                if (out_count < output.size())
                {
                    output[out_count++] = cur;
                }
            }
            else if (prev_inside)
            {
                const double t = (near_plane - prev.view.z) / (cur.view.z - prev.view.z);
                if (out_count < output.size())
                {
                    output[out_count++] = lerp_clip(prev, cur, t);
                }
            }

            prev = cur;
            prev_inside = cur_inside;
        }

        return out_count;
    }

    auto render_quad(const RasterTarget& target,
                     const RasterQuadInput& quad_input,
                     const RasterInputs& inputs) const -> void
    {
        const auto& quad = quad_input.quad;
        const auto& camera_pos = quad_input.camera_pos;

        const Vec3 view_vec = camera_pos - quad.v[0];
        if (quad.normal.dot(view_vec) <= 0.0)
        {
            return;
        }

        std::array<Vec3, 4> view_space{};
        for (int i = 0; i < 4; ++i)
        {
            view_space[i] = quad_input.orientation.apply_inverse(quad.v[i] - camera_pos);
        }

        auto project_vertex = [&](const Vec3& view) {
            const double inv_z = 1.0 / view.z;
            return ScreenVertex{
                static_cast<float>(view.x * inv_z * quad_input.proj_scale_x
                                   + static_cast<double>(target.width) / 2.0),
                static_cast<float>(-view.y * inv_z * quad_input.proj_scale_y
                                   + static_cast<double>(target.height) / 2.0),
                static_cast<float>(view.z)
            };
        };

        auto draw_triangle = [&](const ClipVertex& a, const ClipVertex& b, const ClipVertex& c) {
            const RasterTriangleInput tri{
                .v0 = project_vertex(a.view),
                .v1 = project_vertex(b.view),
                .v2 = project_vertex(c.view),
                .wp0 = a.world,
                .wp1 = b.world,
                .wp2 = c.world,
                .normal = quad.normal,
                .vis0 = a.sky_visibility,
                .vis1 = b.sky_visibility,
                .vis2 = c.sky_visibility
            };
            shade_triangle(target, tri, inputs);
        };

        auto draw_clipped = [&](const std::array<int, 3>& idx) {
            const std::array<ClipVertex, 3> input{{
                {view_space[idx[0]], quad.v[idx[0]], quad.sky_visibility[idx[0]]},
                {view_space[idx[1]], quad.v[idx[1]], quad.sky_visibility[idx[1]]},
                {view_space[idx[2]], quad.v[idx[2]], quad.sky_visibility[idx[2]]}
            }};
            std::array<ClipVertex, 4> clipped{};
            const size_t clipped_count = clip_to_near(Camera::near_plane, input, clipped);
            if (clipped_count < 3)
            {
                return;
            }
            draw_triangle(clipped[0], clipped[1], clipped[2]);
            if (clipped_count == 4)
            {
                draw_triangle(clipped[0], clipped[2], clipped[3]);
            }
        };

        draw_clipped({0, 1, 2});
        draw_clipped({0, 2, 3});
    }

private:
    static auto lerp_clip(const ClipVertex& a, const ClipVertex& b, const double t) -> ClipVertex
    {
        return {
            Vec3::lerp(a.view, b.view, t),
            Vec3::lerp(a.world, b.world, t),
            std::lerp(a.sky_visibility, b.sky_visibility, static_cast<float>(t))
        };
    }

    auto shade_triangle(const RasterTarget& target,
                        const RasterTriangleInput& tri,
                        const RasterInputs& inputs) const -> void
    {
        const auto& v0 = tri.v0;
        const auto& v1 = tri.v1;
        const auto& v2 = tri.v2;
        const Vec3 normal = tri.normal;
        const auto width = target.width;
        const auto height = target.height;

        const auto& ctx = inputs.ctx;
        const auto& albedo = ctx.albedo;

        auto edge = [](const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c) {
            return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
        };

        const float min_x = std::min({v0.x, v1.x, v2.x});
        const float max_x = std::max({v0.x, v1.x, v2.x});
        const float min_y = std::min({v0.y, v1.y, v2.y});
        const float max_y = std::max({v0.y, v1.y, v2.y});

        const int x0 = std::max(0, static_cast<int>(std::floor(min_x)));
        const int x1 = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(max_x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(min_y)));
        const int y1 = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(max_y)));

        const float area = edge(v0, v1, v2);
        if (area == 0.0f) return;

        const bool area_positive = area > 0.0f;
        const float inv_area = 1.0f / area;
        const float inv_z0 = 1.0f / v0.z;
        const float inv_z1 = 1.0f / v1.z;
        const float inv_z2 = 1.0f / v2.z;

        const float w0_a = v2.y - v1.y;
        const float w0_b = v1.x - v2.x;
        const float w0_c = v1.y * v2.x - v1.x * v2.y;

        const float w1_a = v0.y - v2.y;
        const float w1_b = v2.x - v0.x;
        const float w1_c = v2.y * v0.x - v2.x * v0.y;

        const float w2_a = v1.y - v0.y;
        const float w2_b = v0.x - v1.x;
        const float w2_c = v0.y * v1.x - v0.x * v1.y;

        const float start_x = static_cast<float>(x0) + 0.5f + inputs.jitter_x;
        const float start_y = static_cast<float>(y0) + 0.5f + inputs.jitter_y;

        float w0_row = w0_a * start_x + w0_b * start_y + w0_c;
        float w1_row = w1_a * start_x + w1_b * start_y + w1_c;
        float w2_row = w2_a * start_x + w2_b * start_y + w2_c;

        const float w0_a_i = w0_a * inv_z0 * inv_area;
        const float w1_a_i = w1_a * inv_z1 * inv_area;
        const float w2_a_i = w2_a * inv_z2 * inv_area;
        const float w0_b_i = w0_b * inv_z0 * inv_area;
        const float w1_b_i = w1_b * inv_z1 * inv_area;
        const float w2_b_i = w2_b * inv_z2 * inv_area;

        float w0i_row = w0_row * inv_z0 * inv_area;
        float w1i_row = w1_row * inv_z1 * inv_area;
        float w2i_row = w2_row * inv_z2 * inv_area;

        const double ambient_scale = ctx.ambient_light * ctx.material.ambient;
        LinearColor ambient_base = albedo * static_cast<float>(ambient_scale);
        if (ctx.sky_scale > 0.0f)
        {
            const float sky_t = std::clamp(static_cast<float>(normal.y * 0.5 + 0.5), 0.0f, 1.0f);
            const LinearColor sky = LinearColor::lerp(ctx.hemi_ground, ctx.sky_top, sky_t);
            ambient_base = sky * albedo * ctx.sky_scale;
        }

        struct LightTerms
        {
            bool active = false;
            double ndotl = 0.0;
        };
        std::array<LightTerms, 2> light_terms{};
        if (ctx.direct_lighting_enabled)
        {
            for (size_t i = 0; i < ctx.lights.size(); ++i)
            {
                const auto& light = ctx.lights[i];
                const double ndotl = normal.dot(light.dir);
                light_terms[i] = {light.intensity > 0.0 && ndotl > 0.0, ndotl};
            }
        }

        for (int y = y0; y <= y1; ++y, w0_row += w0_b, w1_row += w1_b, w2_row += w2_b,
             w0i_row += w0_b_i, w1i_row += w1_b_i, w2i_row += w2_b_i)
        {
            float w0 = w0_row;
            float w1 = w1_row;
            float w2 = w2_row;
            float w0i = w0i_row;
            float w1i = w1i_row;
            float w2i = w2i_row;

            for (int x = x0; x <= x1; ++x, w0 += w0_a, w1 += w1_a, w2 += w2_a,
                 w0i += w0_a_i, w1i += w1_a_i, w2i += w2_a_i)
            {
                const bool inside = area_positive
                                        ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                        : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                if (!inside) continue;

                const float inv_z = w0i + w1i + w2i;
                if (inv_z <= 0.0f) continue;

                const size_t idx = static_cast<size_t>(y) * width + static_cast<size_t>(x);
                const float depth = 1.0f / inv_z;
                if (depth >= target.zbuffer[idx]) continue;

                target.zbuffer[idx] = depth;
                const float w0p = w0i * depth;
                const float w1p = w1i * depth;
                const float w2p = w2i * depth;
                const float ao_vis = ctx.ambient_occlusion_enabled
                                         ? std::clamp(w0p * tri.vis0 + w1p * tri.vis1
                                                          + w2p * tri.vis2,
                                                      0.0f, 1.0f)
                                         : 1.0f;

                const Vec3 world{
                    w0p * tri.wp0.x + w1p * tri.wp1.x + w2p * tri.wp2.x,
                    w0p * tri.wp0.y + w1p * tri.wp1.y + w2p * tri.wp2.y,
                    w0p * tri.wp0.z + w1p * tri.wp1.z + w2p * tri.wp2.z
                };
                target.world_positions[idx] = world;
                target.world_stamp[idx] = target.frame_index;

                LinearColor direct_sun{0.0f, 0.0f, 0.0f};
                LinearColor direct_moon{0.0f, 0.0f, 0.0f};
                if (ctx.direct_lighting_enabled)
                {
                    const auto& material = ctx.material;
                    const double f0 = std::clamp(material.specular, 0.0, 1.0);
                    const Vec3 view_dir = (ctx.camera_pos - world).normalize();

                    auto eval_light = [&](const size_t light_idx, LinearColor& out_direct) {
                        if (!light_terms[light_idx].active) return;

                        const auto& light = ctx.lights[light_idx];
                        const double ndotl = light_terms[light_idx].ndotl;

                        const Vec3 half_vec = (light.dir + view_dir).normalize();
                        const double vdoth = std::max(0.0, view_dir.dot(half_vec));
                        const double fresnel = LightingEngine::fresnel(vdoth, f0);
                        const double diffuse_scale = std::clamp(1.0 - fresnel, 0.0, 1.0);
                        const double diffuse = ndotl * light.intensity * material.diffuse
                                               * diffuse_scale;
                        LinearColor light_color = albedo * light.color
                                                  * static_cast<float>(diffuse);
                        if (f0 > 0.0)
                        {
                            const double spec_dot = std::max(0.0, normal.dot(half_vec));
                            double spec = LightingEngine::specular_term(spec_dot, vdoth, ndotl,
                                                                        material.shininess, f0);
                            spec = std::clamp(spec * light.intensity, 0.0, 1.0);
                            light_color = light_color + light.color * static_cast<float>(spec);
                        }

                        out_direct = light_color;
                    };

                    eval_light(0, direct_sun);
                    eval_light(1, direct_moon);
                }

                target.sample_colors[idx] = ambient_base * ao_vis;
                target.sample_direct_sun[idx] = direct_sun;
                target.sample_direct_moon[idx] = direct_moon;
                target.sample_normals[idx] = normal;
                target.sample_albedo[idx] = albedo;
                target.sample_ao[idx] = ao_vis;
            }
        }
    }
};
