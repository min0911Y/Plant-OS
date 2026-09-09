#include "test_prelude.hpp"

import render;
import post;

namespace {
bool vertex_sky_visibility(Terrain &terrain, const int x, const int y, const int z,
                           const int face, const int corner, float *out_visibility) {
  if (!out_visibility || face < 0 || face >= 6 || corner < 0 || corner >= 4) {
    return false;
  }
  if (terrain.blocks.empty()) {
    terrain.generate();
  }
  const VoxelBlock *block = terrain.topology.block_at(terrain.blocks, x, y, z);
  if (!block) {
    return false;
  }
  *out_visibility = block->sky_visibility[static_cast<size_t>(face)][static_cast<size_t>(corner)];
  return true;
}

Vec3 tonemap_vec3(const Vec3 color, const double exposure_value)
{
    const float exposure_factor = static_cast<float>(std::max(0.0, exposure_value));
    const LinearColor input{
        static_cast<float>(color.x),
        static_cast<float>(color.y),
        static_cast<float>(color.z)
    };
    const LinearColor mapped = PostProcessor::tonemap_reinhard(input, exposure_factor);
    return {mapped.r, mapped.g, mapped.b};
}

Vec3 sample_history(const Vec3* buffer, const size_t width, const size_t height,
                             const Vec2 screen_coord)
{
    if (!buffer || width == 0 || height == 0)
    {
        return {0.0, 0.0, 0.0};
    }
    const std::span<const Vec3> span(buffer, width * height);

    double x = screen_coord.x - 0.5;
    double y = screen_coord.y - 0.5;
    x = std::clamp(x, 0.0, static_cast<double>(width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, static_cast<int>(width) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(height) - 1);
    const double fx = x - static_cast<double>(x0);
    const double fy = y - static_cast<double>(y0);

    const size_t row0 = static_cast<size_t>(y0) * width;
    const size_t row1 = static_cast<size_t>(y1) * width;
    const Vec3 c00 = span[row0 + static_cast<size_t>(x0)];
    const Vec3 c10 = span[row0 + static_cast<size_t>(x1)];
    const Vec3 c01 = span[row1 + static_cast<size_t>(x0)];
    const Vec3 c11 = span[row1 + static_cast<size_t>(x1)];
    const Vec3 top = Vec3::lerp(c00, c10, fx);
    const Vec3 bottom = Vec3::lerp(c01, c11, fx);
    return Vec3::lerp(top, bottom, fy);
}

bool sample_depth(const Vec3 v0, const Vec3 v1, const Vec3 v2, const Vec2 p, float* out_depth)
{
    if (!out_depth)
    {
        return false;
    }
    const ScreenVertex sv0{static_cast<float>(v0.x), static_cast<float>(v0.y), static_cast<float>(v0.z)};
    const ScreenVertex sv1{static_cast<float>(v1.x), static_cast<float>(v1.y), static_cast<float>(v1.z)};
    const ScreenVertex sv2{static_cast<float>(v2.x), static_cast<float>(v2.y), static_cast<float>(v2.z)};

    const auto edge = [](const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c) {
        return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
    };
    const float area = edge(sv0, sv1, sv2);
    if (area == 0.0f)
    {
        return false;
    }
    const bool area_positive = area > 0.0f;
    const ScreenVertex sp{static_cast<float>(p.x), static_cast<float>(p.y), 0.0f};

    float w0 = edge(sv1, sv2, sp);
    float w1 = edge(sv2, sv0, sp);
    float w2 = edge(sv0, sv1, sp);

    if ((w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f && area_positive) ||
        (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f && !area_positive))
    {
        w0 /= area;
        w1 /= area;
        w2 /= area;
        const float inv_z0 = 1.0f / sv0.z;
        const float inv_z1 = 1.0f / sv1.z;
        const float inv_z2 = 1.0f / sv2.z;
        const float inv_z = w0 * inv_z0 + w1 * inv_z1 + w2 * inv_z2;
        if (inv_z <= 0.0f)
        {
            return false;
        }
        *out_depth = 1.0f / inv_z;
        return true;
    }
    return false;
}

float shadow_filter(const LightingEngine& lighting, const ShadowSettings& shadow_settings,
                    const float* mask, const float* depth, const Vec3* normals)
{
    if (!mask || !depth || !normals)
    {
        return 0.0f;
    }
    const float depth_max = std::numeric_limits<float>::max();
    constexpr size_t kDebugShadowFilterSize = 3 * 3;
    std::array<float, kDebugShadowFilterSize> out_a{};
    std::array<float, kDebugShadowFilterSize> out_b{};
    const std::span<const float> mask_span(mask, kDebugShadowFilterSize);
    const std::span<const float> depth_span(depth, kDebugShadowFilterSize);
    const std::span<const Vec3> normals_span(normals, kDebugShadowFilterSize);
    lighting.filter_shadows(mask_span, mask_span, out_a, out_b,
                            depth_span, normals_span, 3, 3, depth_max, shadow_settings);
    return out_a[4];
}

uint32_t pack_color(const ColorSrgb& color)
{
    auto clamp_channel = [](float value) {
        if (value < 0.0f) value = 0.0f;
        if (value > 255.0f) value = 255.0f;
        return static_cast<uint32_t>(std::lround(value));
    };
    const uint32_t r = clamp_channel(color.r);
    const uint32_t g = clamp_channel(color.g);
    const uint32_t b = clamp_channel(color.b);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

Vec2 project_point(const Camera& camera, const Vec3 world, const size_t width, const size_t height)
{
    if (width == 0 || height == 0)
    {
        return {0.0, 0.0};
    }

    const Vec3 view = camera.to_camera_space(world);
    if (view.z <= Camera::near_plane)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan};
    }

    const double inv_z = 1.0 / view.z;
    const double proj_scale = static_cast<double>(height) * 0.8;
    const double proj_x = view.x * inv_z * proj_scale;
    const double proj_y = -view.y * inv_z * proj_scale;

    return {proj_x + static_cast<double>(width) / 2.0,
            proj_y + static_cast<double>(height) / 2.0};
}

Vec3 unproject_point(const Camera& camera, const Vec3 screen, const size_t width, const size_t height)
{
    if (width == 0 || height == 0)
    {
        return {0.0, 0.0, 0.0};
    }

    const double proj_scale = static_cast<double>(height) * 0.8;
    const double half_w = static_cast<double>(width) / 2.0;
    const double half_h = static_cast<double>(height) / 2.0;

    const double view_x = (screen.x - half_w) / proj_scale * screen.z;
    const double view_y = -(screen.y - half_h) / proj_scale * screen.z;
    const double view_z = screen.z;

    return camera.from_camera_space({view_x, view_y, view_z});
}

Vec2 reproject_point(const Mat4& previous_vp, const Vec3 world, const size_t width, const size_t height)
{
    if (width == 0 || height == 0)
    {
        return {0.0, 0.0};
    }

    const double clip_x = previous_vp.m[0][0] * world.x + previous_vp.m[0][1] * world.y + previous_vp.m[0][2] * world.z + previous_vp.m[0][3];
    const double clip_y = previous_vp.m[1][0] * world.x + previous_vp.m[1][1] * world.y + previous_vp.m[1][2] * world.z + previous_vp.m[1][3];
    const double clip_w = previous_vp.m[3][0] * world.x + previous_vp.m[3][1] * world.y + previous_vp.m[3][2] * world.z + previous_vp.m[3][3];

    if (clip_w <= Camera::near_plane)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan};
    }

    const double inv_w = 1.0 / clip_w;
    const double ndc_x = clip_x * inv_w;
    const double ndc_y = clip_y * inv_w;
    const double screen_x = (ndc_x * 0.5 + 0.5) * static_cast<double>(width);
    const double screen_y = (ndc_y * 0.5 + 0.5) * static_cast<double>(height);
    return {screen_x, screen_y};
}

bool should_rasterize(const Rasterizer& rasterizer, const Vec3 v0, const Vec3 v1, const Vec3 v2)
{
    (void)rasterizer;
    return v0.z >= Camera::near_plane || v1.z >= Camera::near_plane || v2.z >= Camera::near_plane;
}

size_t clip_near(const Rasterizer& rasterizer,
                                   const Vec3 v0, const Vec3 v1, const Vec3 v2,
                                   Vec3* out_vertices, const size_t max_vertices)
{
    if (!out_vertices || max_vertices == 0)
    {
        return 0;
    }
    const std::array<ClipVertex, 3> input{{
        {v0, v0, 0.0f},
        {v1, v1, 0.0f},
        {v2, v2, 0.0f}
    }};
    std::array<ClipVertex, 4> clipped{};
    const size_t count = rasterizer.clip_to_near(Camera::near_plane, input, clipped);
    const size_t out_count = std::min(count, max_vertices);
    for (size_t i = 0; i < out_count; ++i)
    {
        out_vertices[i] = clipped[i].view;
    }
    return out_count;
}
}

static void reset_camera(RenderEngine& engine)
{
    engine.camera.position = {0.0, 0.0, -4.0};
    engine.camera.set_rotation({0.0, 0.0});
    engine.world.sky.day_zenith = ColorSrgb::from_hex(0xFF6FB7FF).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(0xFFBFDFFF).to_linear();
    engine.world.sky.sky_light_scale = 0.0;
    engine.settings.ambient_occlusion_enabled = true;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.orbit_angle = 0.0;
    engine.world.moon.direction = {0.0, 1.0, 0.0};
    engine.world.moon.intensity = 0.0;
    engine.settings.shadow_enabled = true;
    engine.world.sky.exposure = std::max(0.0, 1.0);
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.2;
    engine.settings.taa.clamp_enabled = true;
    engine.settings.gi.enabled = false;
    engine.settings.gi.strength = 0.0;
}

static bool mat4_near(const Mat4& a, const Mat4& b, const double eps)
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            if (std::fabs(a.m[i][j] - b.m[i][j]) > eps)
            {
                return false;
            }
        }
    }
    return true;
}

static std::string mat4_str(const Mat4& m)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(6);
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            oss << m.m[i][j];
            if (j < 3)
            {
                oss << ", ";
            }
        }
        if (i < 3)
        {
            oss << '\n';
        }
    }
    return oss.str();
}

static Vec3 rotate_yaw_pitch(const Vec3& v, const double yaw, const double pitch)
{
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double x1 = v.x * cy + v.z * sy;
    const double z1 = -v.x * sy + v.z * cy;
    const double y1 = v.y;
    const double y2 = y1 * cp - z1 * sp;
    const double z2 = y1 * sp + z1 * cp;
    return {x1, y2, z2};
}

static bool is_nan(const double value)
{
    const uint64_t bits = std::bit_cast<uint64_t>(value);
    const uint64_t exp = bits & 0x7ff0000000000000ULL;
    const uint64_t mantissa = bits & 0x000fffffffffffffULL;
    return exp == 0x7ff0000000000000ULL && mantissa != 0ULL;
}

static bool is_finite(const double value)
{
    const uint64_t bits = std::bit_cast<uint64_t>(value);
    const uint64_t exp = bits & 0x7ff0000000000000ULL;
    return exp != 0x7ff0000000000000ULL;
}

static float srgb_to_linear(float channel);
static float linear_to_srgb(float channel);

static uint32_t sky_row_color(size_t y, size_t height, uint32_t sky_top, uint32_t sky_bottom,
                                  double exposure)
{
    const float t = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
    const uint32_t r0 = (sky_top >> 16) & 0xFF;
    const uint32_t g0 = (sky_top >> 8) & 0xFF;
    const uint32_t b0 = sky_top & 0xFF;
    const uint32_t r1 = (sky_bottom >> 16) & 0xFF;
    const uint32_t g1 = (sky_bottom >> 8) & 0xFF;
    const uint32_t b1 = sky_bottom & 0xFF;
    const float r0_lin = srgb_to_linear(static_cast<float>(r0));
    const float g0_lin = srgb_to_linear(static_cast<float>(g0));
    const float b0_lin = srgb_to_linear(static_cast<float>(b0));
    const float r1_lin = srgb_to_linear(static_cast<float>(r1));
    const float g1_lin = srgb_to_linear(static_cast<float>(g1));
    const float b1_lin = srgb_to_linear(static_cast<float>(b1));
    const float r_lin = r0_lin + (r1_lin - r0_lin) * t;
    const float g_lin = g0_lin + (g1_lin - g0_lin) * t;
    const float b_lin = b0_lin + (b1_lin - b0_lin) * t;
    const float exposure_f = static_cast<float>(std::max(0.0, exposure));
    const float r_mapped = exposure_f <= 0.0f ? 0.0f : (r_lin * exposure_f) / (1.0f + r_lin * exposure_f);
    const float g_mapped = exposure_f <= 0.0f ? 0.0f : (g_lin * exposure_f) / (1.0f + g_lin * exposure_f);
    const float b_mapped = exposure_f <= 0.0f ? 0.0f : (b_lin * exposure_f) / (1.0f + b_lin * exposure_f);
    const uint32_t r = static_cast<uint32_t>(std::lround(linear_to_srgb(r_mapped) * 255.0f));
    const uint32_t g = static_cast<uint32_t>(std::lround(linear_to_srgb(g_mapped) * 255.0f));
    const uint32_t b = static_cast<uint32_t>(std::lround(linear_to_srgb(b_mapped) * 255.0f));
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static uint32_t luminance(uint32_t color)
{
    return ((color >> 16) & 0xFF) + ((color >> 8) & 0xFF) + (color & 0xFF);
}

static void build_heightmap(std::vector<int>& heights, std::vector<uint32_t>& top_colors)
{
    const int chunk_size = 16;
    const int base_height = 4;
    const int height_variation = 6;
    const double height_freq = 0.12;
    const double surface_freq = 0.4;
    const uint32_t dirt_color = 0xFF8A4F22;
    const uint32_t grass_color = 0xFF3B8A38;
    const uint32_t water_color = 0xFF2B5FA8;

    heights.assign(static_cast<size_t>(chunk_size * chunk_size), 0);
    top_colors.assign(static_cast<size_t>(chunk_size * chunk_size), grass_color);

    auto index = [chunk_size](int x, int z) {
        return static_cast<size_t>(z * chunk_size + x);
    };

    for (int z = 0; z < chunk_size; ++z)
    {
        for (int x = 0; x < chunk_size; ++x)
        {
            const double h = SimplexNoise::sample(x * height_freq, z * height_freq);
            int height = base_height + static_cast<int>(std::lround((h + 1.0) * 0.5 * height_variation));
            if (height < 3)
            {
                height = 3;
            }

            const double surface = SimplexNoise::sample(x * surface_freq + 100.0, z * surface_freq - 100.0);
            uint32_t top_color = grass_color;
            if (surface > 0.55)
            {
                top_color = water_color;
            }
            else if (surface < -0.35)
            {
                top_color = dirt_color;
            }

            heights[index(x, z)] = height;
            top_colors[index(x, z)] = top_color;
        }
    }
}

static bool find_shadow_sample(const std::vector<int>& heights, const Vec3& light_dir,
                               const Vec3& normal, const bool want_shadow, Vec3* out_world)
{
    if (!out_world)
    {
        return false;
    }
    const int chunk_size = 16;
    const double block_size = 2.0;
    const double half = block_size * 0.5;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    auto index = [chunk_size](int x, int z) {
        return static_cast<size_t>(z * chunk_size + x);
    };

    auto heightmap_has_block = [&](int gx, int gy, int gz) {
        if (gx < 0 || gx >= chunk_size || gz < 0 || gz >= chunk_size || gy < 0)
        {
            return false;
        }
        const size_t idx = static_cast<size_t>(gz * chunk_size + gx);
        if (idx >= heights.size())
        {
            return false;
        }
        return gy < heights[idx];
    };

    auto heightmap_max_height = [&]() {
        int max_height = 0;
        for (int value : heights)
        {
            if (value > max_height)
            {
                max_height = value;
            }
        }
        return max_height;
    };

    auto world_to_grid_coords = [block_size, half, start_x, start_z, base_y](const Vec3& pos) {
        return Vec3{
            (pos.x - start_x + half) / block_size,
            (pos.y - base_y + half) / block_size,
            (pos.z - start_z + half) / block_size
        };
    };

    auto raymarch_shadow_hit = [&](const Vec3& world) {
        const int max_height = heightmap_max_height();
        if (max_height <= 0)
        {
            return false;
        }

        const double step = 0.1;
        const int max_steps = 2000;
        const double bias = 0.05;
        Vec3 pos{
            world.x + normal.x * bias,
            world.y + normal.y * bias,
            world.z + normal.z * bias
        };

        for (int i = 0; i < max_steps; ++i)
        {
            pos.x += light_dir.x * step;
            pos.y += light_dir.y * step;
            pos.z += light_dir.z * step;

            const Vec3 grid = world_to_grid_coords(pos);
            if (grid.x < 0.0 || grid.x >= chunk_size ||
                grid.z < 0.0 || grid.z >= chunk_size ||
                grid.y < 0.0 || grid.y >= max_height)
            {
                return false;
            }

            const int gx = static_cast<int>(std::floor(grid.x));
            const int gy = static_cast<int>(std::floor(grid.y));
            const int gz = static_cast<int>(std::floor(grid.z));
            if (heightmap_has_block(gx, gy, gz))
            {
                return true;
            }
        }
        return false;
    };

    for (int z = 0; z < chunk_size; ++z)
    {
        for (int x = 0; x < chunk_size; ++x)
        {
            const int height = heights[index(x, z)];
            if (height <= 0)
            {
                continue;
            }
            const double center_y = base_y + (height - 1) * block_size;
            const double top_y = center_y + block_size * 0.5;
            const Vec3 world{
                start_x + x * block_size,
                top_y,
                start_z + z * block_size
            };
            const bool hit = raymarch_shadow_hit(world);
            if (hit == want_shadow)
            {
                *out_world = world;
                return true;
            }
        }
    }
    return false;
}

static bool terrain_has_block(const std::vector<int>& heights, const int chunk_size,
                              const int gx, const int gy, const int gz);

constexpr int kFaceTop = 0;
constexpr int kFaceBottom = 1;
constexpr int kFaceLeft = 2;
constexpr int kFaceRight = 3;
constexpr int kFaceBack = 4;
constexpr int kFaceFront = 5;

constexpr int kCubeFaceNormal[6][3] = {
    {0, 1, 0},
    {0, -1, 0},
    {-1, 0, 0},
    {1, 0, 0},
    {0, 0, -1},
    {0, 0, 1}
};

constexpr int kCubeFaceVertices[6][4] = {
    {3, 7, 6, 2},
    {0, 1, 5, 4},
    {0, 4, 7, 3},
    {1, 2, 6, 5},
    {0, 3, 2, 1},
    {4, 5, 6, 7}
};

constexpr int kCubeVertexGrid[8][3] = {
    {0, 0, 0},
    {1, 0, 0},
    {1, 1, 0},
    {0, 1, 0},
    {0, 0, 1},
    {1, 0, 1},
    {1, 1, 1},
    {0, 1, 1}
};

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kSkyRayCount = 128;
constexpr double kSkyRayStep = 0.25;
constexpr double kSkyRayMaxDistance = 6.0;
constexpr double kSkyRayBias = 0.02;
constexpr double kSkyRayCenterBias = 0.02;

static Vec3 normalize_vec(const Vec3& v)
{
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len == 0.0) return {0.0, 0.0, 0.0};
    return {v.x / len, v.y / len, v.z / len};
}

static float raycast_vertex_sky_visibility(const std::vector<int>& heights, const int chunk_size,
                                           const int gx, const int gy, const int gz,
                                           const int face, const int corner)
{
    auto build_basis = [](const Vec3& n, Vec3& t, Vec3& b) {
        Vec3 up{0.0, 1.0, 0.0};
        if (std::abs(n.x * up.x + n.y * up.y + n.z * up.z) > 0.99)
        {
            up = {1.0, 0.0, 0.0};
        }
        t = normalize_vec({up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x});
        b = {n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x};
    };

    auto face_corner_position_grid = [](int gx, int gy, int gz, int face, int corner) {
        const int vi = kCubeFaceVertices[face][corner];
        return Vec3{
            static_cast<double>(gx + kCubeVertexGrid[vi][0]),
            static_cast<double>(gy + kCubeVertexGrid[vi][1]),
            static_cast<double>(gz + kCubeVertexGrid[vi][2])
        };
    };

    auto sky_sample_dirs = []() -> const std::vector<Vec3>& {
        static std::vector<Vec3> samples = [] {
            auto radical_inverse_vdc = [](uint32_t bits) {
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                return static_cast<double>(bits) * 2.3283064365386963e-10;
            };

            std::vector<Vec3> dirs;
            dirs.reserve(kSkyRayCount);
            for (size_t i = 0; i < kSkyRayCount; ++i)
            {
                const double u = (static_cast<double>(i) + 0.5) / static_cast<double>(kSkyRayCount);
                const double v = radical_inverse_vdc(static_cast<uint32_t>(i));
                const double r = std::sqrt(u);
                const double theta = 2.0 * kPi * v;
                const double x = r * std::cos(theta);
                const double y = r * std::sin(theta);
                const double z = std::sqrt(std::max(0.0, 1.0 - u));
                dirs.push_back({x, y, z});
            }
            return dirs;
        }();
        return samples;
    };

    const Vec3 normal = normalize_vec({
        static_cast<double>(kCubeFaceNormal[face][0]),
        static_cast<double>(kCubeFaceNormal[face][1]),
        static_cast<double>(kCubeFaceNormal[face][2])
    });
    Vec3 tangent{};
    Vec3 bitangent{};
    build_basis(normal, tangent, bitangent);

    const Vec3 vertex = face_corner_position_grid(gx, gy, gz, face, corner);
    const Vec3 center{
        static_cast<double>(gx) + 0.5,
        static_cast<double>(gy) + 0.5,
        static_cast<double>(gz) + 0.5
    };
    Vec3 origin = vertex;
    origin.x += normal.x * kSkyRayBias;
    origin.y += normal.y * kSkyRayBias;
    origin.z += normal.z * kSkyRayBias;
    origin.x += (center.x - vertex.x) * kSkyRayCenterBias;
    origin.y += (center.y - vertex.y) * kSkyRayCenterBias;
    origin.z += (center.z - vertex.z) * kSkyRayCenterBias;

    const auto& samples = sky_sample_dirs();
    size_t occluded = 0;
    for (const auto& sample : samples)
    {
        Vec3 dir{
            tangent.x * sample.x + bitangent.x * sample.y + normal.x * sample.z,
            tangent.y * sample.x + bitangent.y * sample.y + normal.y * sample.z,
            tangent.z * sample.x + bitangent.z * sample.y + normal.z * sample.z
        };

        bool hit = false;
        for (double t = kSkyRayStep; t <= kSkyRayMaxDistance; t += kSkyRayStep)
        {
            const Vec3 p{
                origin.x + dir.x * t,
                origin.y + dir.y * t,
                origin.z + dir.z * t
            };
            const int vx = static_cast<int>(std::floor(p.x));
            const int vy = static_cast<int>(std::floor(p.y));
            const int vz = static_cast<int>(std::floor(p.z));
            if (terrain_has_block(heights, chunk_size, vx, vy, vz))
            {
                hit = true;
                break;
            }
        }
        if (hit)
        {
            occluded++;
        }
    }

    const double visibility = 1.0 - static_cast<double>(occluded) / static_cast<double>(samples.size());
    return static_cast<float>(std::clamp(visibility, 0.0, 1.0));
}

static bool terrain_has_block(const std::vector<int>& heights, const int chunk_size,
                              const int gx, const int gy, const int gz)
{
    if (gx < 0 || gx >= chunk_size || gz < 0 || gz >= chunk_size)
    {
        return false;
    }
    if (gy < 0)
    {
        return false;
    }
    const size_t idx = static_cast<size_t>(gz * chunk_size + gx);
    return gy < heights[idx];
}

static bool find_sloped_grass(int& out_x, int& out_z, int& out_height)
{
    const int chunk_size = 16;
    const uint32_t grass_color = 0xFF3B8A38;

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    auto index = [chunk_size](int x, int z) {
        return static_cast<size_t>(z * chunk_size + x);
    };

    int best_slope = -1;
    int best_x = 0;
    int best_z = 0;
    int best_height = 0;

    for (int z = 1; z < chunk_size - 1; ++z)
    {
        for (int x = 1; x < chunk_size - 1; ++x)
        {
            if (top_colors[index(x, z)] != grass_color)
            {
                continue;
            }
            const int h_l = heights[index(x - 1, z)];
            const int h_r = heights[index(x + 1, z)];
            const int h_d = heights[index(x, z - 1)];
            const int h_u = heights[index(x, z + 1)];
            const int slope = std::abs(h_r - h_l) + std::abs(h_u - h_d);
            if (slope > best_slope)
            {
                best_slope = slope;
                best_x = x;
                best_z = z;
                best_height = heights[index(x, z)];
            }
        }
    }

    if (best_slope <= 0)
    {
        return false;
    }

    out_x = best_x;
    out_z = best_z;
    out_height = best_height;
    return true;
}

struct SideFaceAoProbe
{
    int x;
    int z;
    int height;
};

static bool find_right_face_occluder(SideFaceAoProbe& out_probe)
{
    const int chunk_size = 16;
    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    auto index = [chunk_size](int x, int z) {
        return static_cast<size_t>(z * chunk_size + x);
    };

    for (int z = 0; z < chunk_size - 1; ++z)
    {
        for (int x = 0; x < chunk_size - 1; ++x)
        {
            const int height = heights[index(x, z)];
            const int y = height - 1;
            if (y < 0)
            {
                continue;
            }
            const int right_height = heights[index(x + 1, z)];
            const int z_height = heights[index(x, z + 1)];
            const int diag_height = heights[index(x + 1, z + 1)];
            if (right_height != height - 1)
            {
                continue;
            }
            if (z_height > y)
            {
                continue;
            }
            if (diag_height <= y)
            {
                continue;
            }
            out_probe = {x, z, height};
            return true;
        }
    }

    return false;
}

static bool average_luminance(const std::vector<uint32_t>& frame, size_t width, size_t height,
                                     int px, int py, uint32_t sky_top, uint32_t sky_bottom,
                                     double exposure, double& out_avg)
{
    const int radius = 4;
    double sum = 0.0;
    size_t count = 0;
    for (int dy = -radius; dy <= radius; ++dy)
    {
        const int y = std::clamp(py + dy, 0, static_cast<int>(height) - 1);
        const uint32_t sky = sky_row_color(static_cast<size_t>(y), height, sky_top, sky_bottom, exposure);
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int x = std::clamp(px + dx, 0, static_cast<int>(width) - 1);
            const uint32_t pixel = frame[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
            if (pixel == sky)
            {
                continue;
            }
            sum += static_cast<double>(luminance(pixel));
            count++;
        }
    }

    if (count == 0)
    {
        return false;
    }
    out_avg = sum / static_cast<double>(count);
    return true;
}

static bool average_color(const std::vector<uint32_t>& frame, size_t width, size_t height,
                                 int px, int py, uint32_t sky_top, uint32_t sky_bottom,
                                 double exposure, double& out_r, double& out_g, double& out_b)
{
    const int radius = 4;
    double sum_r = 0.0;
    double sum_g = 0.0;
    double sum_b = 0.0;
    size_t count = 0;
    for (int dy = -radius; dy <= radius; ++dy)
    {
        const int y = std::clamp(py + dy, 0, static_cast<int>(height) - 1);
        const uint32_t sky = sky_row_color(static_cast<size_t>(y), height, sky_top, sky_bottom, exposure);
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int x = std::clamp(px + dx, 0, static_cast<int>(width) - 1);
            const uint32_t pixel = frame[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
            if (pixel == sky)
            {
                continue;
            }
            sum_r += static_cast<double>((pixel >> 16) & 0xFF);
            sum_g += static_cast<double>((pixel >> 8) & 0xFF);
            sum_b += static_cast<double>(pixel & 0xFF);
            count++;
        }
    }

    if (count == 0)
    {
        return false;
    }
    out_r = sum_r / static_cast<double>(count);
    out_g = sum_g / static_cast<double>(count);
    out_b = sum_b / static_cast<double>(count);
    return true;
}

static float srgb_to_linear(float channel)
{
    const float c = channel / 255.0f;
    if (c <= 0.04045f)
    {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float channel)
{
    channel = std::clamp(channel, 0.0f, 1.0f);
    if (channel <= 0.0031308f)
    {
        return channel * 12.92f;
    }
    return 1.055f * std::pow(channel, 1.0f / 2.4f) - 0.055f;
}

TEST_CASE("blue noise sampling responds to salt and frame")
{
    const float base = BlueNoise::sample(5, 7, 0, 0);
    REQUIRE(base >= 0.0f);
    REQUIRE(base < 1.0f);

    int same_salt = 0;
    int same_frame = 0;
    int total = 0;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const float a = BlueNoise::sample(x, y, 0, 0);
            const float b = BlueNoise::sample(x, y, 0, 1);
            const float c = BlueNoise::sample(x, y, 1, 0);
            if (a == b) same_salt++;
            if (a == c) same_frame++;
            total++;
        }
    }

    REQUIRE(same_salt < total);
    REQUIRE(same_frame < total);
}

TEST_CASE("shadow spatial filter applies cross blur with bilateral rejection")
{
    LightingEngine lighting;
    RenderSettings settings;
    const std::array<float, 9> mask = {1.0f, 1.0f, 1.0f,
                                       1.0f, 0.0f, 1.0f,
                                       1.0f, 1.0f, 1.0f};
    std::array<float, 9> depth_same{};
    depth_same.fill(5.0f);
    std::array<Vec3, 9> normals_same{};
    for (auto& n : normals_same)
    {
        n = {0.0, 1.0, 0.0};
    }

    const float baseline = shadow_filter(lighting, settings.shadow,
                                         mask.data(), depth_same.data(), normals_same.data());
    REQUIRE(baseline == Catch::Approx(0.5f).margin(0.02f));

    std::array<float, 9> depth_far = depth_same;
    for (size_t i = 0; i < depth_far.size(); ++i)
    {
        if (i != 4)
        {
            depth_far[i] = 50.0f;
        }
    }
    const float depth_filtered = shadow_filter(lighting, settings.shadow,
                                               mask.data(), depth_far.data(), normals_same.data());
    REQUIRE(depth_filtered < baseline);
    REQUIRE(depth_filtered < 0.2f);

    std::array<Vec3, 9> normals_flipped = normals_same;
    for (size_t i = 0; i < normals_flipped.size(); ++i)
    {
        if (i != 4)
        {
            normals_flipped[i] = {0.0, -1.0, 0.0};
        }
    }
    const float normal_filtered = shadow_filter(lighting, settings.shadow,
                                                mask.data(), depth_same.data(), normals_flipped.data());
    REQUIRE(normal_filtered < baseline);
    REQUIRE(normal_filtered < 0.2f);
}

TEST_CASE("RenderEngine update clears framebuffer and draws geometry")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = false;
    engine.world.sun.direction = {0.0, 0.0, -1.0};
    engine.world.sun.intensity = 1.0;
    const size_t width = 120;
    const size_t height = 80;

    std::vector<uint32_t> framebuffer(width * height, 0xFFFFFFFF);
    engine.update(framebuffer.data(), width, height);

    auto count_geometry_pixels = [](const std::vector<uint32_t>& framebuffer, size_t width, size_t height,
                                    uint32_t sky_top, uint32_t sky_bottom, double exposure) {
        size_t count = 0;
        for (size_t y = 0; y < height; ++y)
        {
            const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom, exposure);
            for (size_t x = 0; x < width; ++x)
            {
                if (framebuffer[y * width + x] != sky)
                {
                    count++;
                }
            }
        }
        return count;
    };

    for (const uint32_t pixel : framebuffer)
    {
        REQUIRE((pixel & 0xFF000000u) == 0xFF000000u);
    }

    const size_t colored = count_geometry_pixels(framebuffer, width, height,
                                                 0xFF78C2FF, 0xFF172433,
                                                 engine.world.sky.exposure);
    REQUIRE(colored > 0);
}

TEST_CASE("RenderEngine update handles tiny even-sized buffers")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = false;
    engine.world.sun.direction = {0.0, 0.0, 1.0};
    engine.world.sun.intensity = 1.0;
    const size_t width = 2;
    const size_t height = 2;

    std::vector<uint32_t> framebuffer(width * height, 0xFFFFFFFF);
    engine.update(framebuffer.data(), width, height);

    for (const uint32_t pixel : framebuffer)
    {
        REQUIRE((pixel & 0xFF000000u) == 0xFF000000u);
    }
}

TEST_CASE("pause stops sun orbit but keeps rendering active")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 64;
    const size_t height = 64;

    engine.world.sun.direction = {0.0, 0.0, 1.0};
    engine.world.sun.intensity = 1.0;
    engine.world.sun.orbit_enabled = true;
    engine.world.sun.orbit_angle = 0.5;

    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.settings.paused = false;
    for (int i = 0; i < 4; ++i)
    {
        engine.update(framebuffer.data(), width, height);
    }
    const double moved_angle = engine.world.sun.orbit_angle;
    REQUIRE(std::abs(moved_angle - 0.5) > 1e-4);

    engine.settings.paused = true;
    const double paused_angle = engine.world.sun.orbit_angle;
    for (int i = 0; i < 4; ++i)
    {
        engine.update(framebuffer.data(), width, height);
    }
    engine.settings.paused = false;

    REQUIRE(engine.world.sun.orbit_angle == Catch::Approx(paused_angle));
}

TEST_CASE("temporal accumulation reduces frame-to-frame noise")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.intensity = 1.0;
    engine.world.moon.intensity = 0.0;
    engine.settings.shadow_enabled = true;
    engine.world.sky.sky_light_scale = 0.0;
    engine.settings.ambient_occlusion_enabled = false;
    engine.world.sky.exposure = std::max(0.0, 1.0);
    engine.settings.paused = false;

    const size_t width = 200;
    const size_t height = 160;
    const uint32_t sky_top = 0xFF78C2FF;
    const uint32_t sky_bottom = 0xFF172433;

    struct Config
    {
        Vec3 camera_pos;
        Vec2 camera_rot;
        Vec3 light_dir;
    };

    const std::array<Config, 3> configs = {{
        {{0.0, 0.0, -4.0}, {0.0, 0.0}, {0.6, 0.3, 0.8}},
        {{0.0, -10.0, -12.0}, {0.0, 0.5}, {0.9, 0.2, 0.3}},
        {{0.0, -22.0, -10.0}, {0.0, 0.8}, {0.4, 0.5, 0.8}}
    }};

    auto max_luminance_delta = [&](const std::vector<uint32_t>& a,
                                   const std::vector<uint32_t>& b) {
        double max_delta = 0.0;
        for (size_t y = 0; y < height; ++y)
        {
            const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom,
                                                   engine.world.sky.exposure);
            for (size_t x = 0; x < width; ++x)
            {
                const size_t idx = y * width + x;
                if (a[idx] == sky || b[idx] == sky)
                {
                    continue;
                }
                const double delta = std::abs(static_cast<double>(luminance(a[idx])) -
                                              static_cast<double>(luminance(b[idx])));
                if (delta > max_delta)
                {
                    max_delta = delta;
                }
            }
        }
        return max_delta;
    };

    std::vector<uint32_t> frame0(width * height, 0u);
    std::vector<uint32_t> frame1(width * height, 0u);

    double delta_no_taa = 0.0;
    bool found = false;
    for (const auto& cfg : configs)
    {
        engine.camera.position = cfg.camera_pos;
        engine.camera.set_rotation(cfg.camera_rot);
        engine.world.sun.direction = cfg.light_dir;

        engine.settings.taa.enabled = false;
        engine.update(frame0.data(), width, height);
        engine.update(frame1.data(), width, height);

        delta_no_taa = max_luminance_delta(frame0, frame1);
        if (delta_no_taa > 0.0)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        SUCCEED("No temporal noise detected in the current configurations.");
        return;
    }

    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.2;

    std::vector<uint32_t> scratch(width * height, 0u);
    engine.update(scratch.data(), width, height);
    engine.update(scratch.data(), width, height);

    std::vector<uint32_t> frame2(width * height, 0u);
    std::vector<uint32_t> frame3(width * height, 0u);
    engine.update(frame2.data(), width, height);
    engine.update(frame3.data(), width, height);

    const double delta_taa = max_luminance_delta(frame2, frame3);
    REQUIRE(delta_taa < delta_no_taa);
}

TEST_CASE("RenderEngine update stabilizes jittered pixels when camera is static")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.1;
    engine.settings.taa.clamp_enabled = true;

    const size_t width = 160;
    const size_t height = 120;
    const uint32_t sky_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t sky_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());

    std::vector<uint32_t> warm(width * height, 0u);
    engine.update(warm.data(), width, height);
    engine.update(warm.data(), width, height);

    std::vector<uint32_t> frame_a(width * height, 0u);
    std::vector<uint32_t> frame_b(width * height, 0u);
    engine.update(frame_a.data(), width, height);
    engine.update(frame_b.data(), width, height);

    auto average_luminance_delta_masked = [](const std::vector<uint32_t>& a,
                                             const std::vector<uint32_t>& b,
                                             size_t width, size_t height,
                                             uint32_t sky_top, uint32_t sky_bottom,
                                             double exposure) {
        double sum = 0.0;
        size_t count = 0;
        for (size_t y = 0; y < height; ++y)
        {
            const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom, exposure);
            for (size_t x = 0; x < width; ++x)
            {
                const size_t idx = y * width + x;
                if (a[idx] == sky && b[idx] == sky)
                {
                    continue;
                }
                const uint32_t la = luminance(a[idx]);
                const uint32_t lb = luminance(b[idx]);
                sum += static_cast<double>(la > lb ? la - lb : lb - la);
                count++;
            }
        }
        if (count == 0)
        {
            return 0.0;
        }
        return sum / static_cast<double>(count);
    };

    auto max_luminance_delta = [&](const std::vector<uint32_t>& a,
                                   const std::vector<uint32_t>& b) {
        double max_delta = 0.0;
        for (size_t y = 0; y < height; ++y)
        {
            const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom,
                                                   engine.world.sky.exposure);
            for (size_t x = 0; x < width; ++x)
            {
                const size_t idx = y * width + x;
                if (a[idx] == sky || b[idx] == sky)
                {
                    continue;
                }
                const double delta = std::abs(static_cast<double>(luminance(a[idx])) -
                                              static_cast<double>(luminance(b[idx])));
                if (delta > max_delta)
                {
                    max_delta = delta;
                }
            }
        }
        return max_delta;
    };

    const double average_delta = average_luminance_delta_masked(frame_a, frame_b,
                                                                width, height,
                                                                sky_top, sky_bottom,
                                                                engine.world.sky.exposure);
    const double max_delta = max_luminance_delta(frame_a, frame_b);

    REQUIRE(average_delta < 2.0);
    REQUIRE(max_delta < 64.0);

    engine.settings.paused = false;
}

TEST_CASE("taa sharpening stays disabled when camera is static")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.1;
    engine.settings.taa.clamp_enabled = true;

    const size_t width = 120;
    const size_t height = 90;
    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.update(framebuffer.data(), width, height);
    engine.update(framebuffer.data(), width, height);

    const double strength = engine.post.sharpen_strength;
    REQUIRE(strength == Catch::Approx(0.0).margin(1e-6));

    engine.settings.paused = false;
}

TEST_CASE("taa sharpening ramps with camera motion")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.1;
    engine.settings.taa.clamp_enabled = true;

    const size_t width = 120;
    const size_t height = 90;
    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.update(framebuffer.data(), width, height);
    engine.update(framebuffer.data(), width, height);

    engine.camera.set_rotation({0.005, 0.0});
    engine.update(framebuffer.data(), width, height);
    const double small = engine.post.sharpen_strength;
    REQUIRE(small > 0.0);

    engine.camera.set_rotation({0.02, 0.0});
    engine.update(framebuffer.data(), width, height);
    const double large = engine.post.sharpen_strength;
    REQUIRE(large > small);

    engine.camera.set_rotation({0.05, 0.0});
    engine.update(framebuffer.data(), width, height);
    double saturated = engine.post.sharpen_strength;
    REQUIRE(saturated > large);

    for (int i = 0; i < 4; ++i)
    {
        engine.camera.set_rotation({0.05 * (i + 2), 0.0});
        engine.update(framebuffer.data(), width, height);
        saturated = engine.post.sharpen_strength;
    }

    const double saturated_pct = engine.post.sharpen_percent();
    REQUIRE(saturated_pct > 95.0);

    engine.settings.paused = false;
}

TEST_CASE("temporal history clamping prevents ghosting after sudden color changes")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.camera.set_rotation({0.0, 1.2});
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.1;
    engine.settings.taa.clamp_enabled = true;

    const size_t width = 96;
    const size_t height = 72;
    std::vector<uint32_t> frame(width * height, 0u);
    const uint32_t original_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t original_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());

    const uint32_t red = 0xFFFF0000;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(red).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(red).to_linear();
    engine.update(frame.data(), width, height);

    const uint32_t green = 0xFF00FF00;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(green).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(green).to_linear();
    engine.update(frame.data(), width, height);

    const size_t sample_x = width / 2;
    const size_t sample_y = 0;
    const uint32_t expected = sky_row_color(sample_y, height, green, green,
                                                engine.world.sky.exposure);
    REQUIRE(frame[sample_y * width + sample_x] == expected);

    engine.world.sky.day_zenith = ColorSrgb::from_hex(original_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(original_bottom).to_linear();
    engine.settings.paused = false;
}

TEST_CASE("RenderEngine update fills front face")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 120;
    const size_t height = 80;

    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.world.sun.direction = {0.0, 0.0, 1.0};
    engine.world.sun.intensity = 1.0;
    engine.settings.paused = true;
    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;
    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const Vec3 probe{
        start_x + (chunk_size / 2) * block_size,
        base_y,
        start_z
    };
    const Vec2 projected = project_point(engine.camera, probe, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);
    const uint32_t sky = sky_row_color(static_cast<size_t>(py), height, 0xFF78C2FF, 0xFF172433,
                                           engine.world.sky.exposure);
    REQUIRE(framebuffer[static_cast<size_t>(py) * width + static_cast<size_t>(px)] != sky);
}

TEST_CASE("RenderEngine update shows multiple terrain materials")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 160;
    const size_t height = 120;

    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.settings.paused = true;
    engine.world.sun.direction = {0.0, 1.0, 1.0};
    engine.world.sun.intensity = 0.0;
    engine.world.sky.sky_light_scale = 1.0;

    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;

    const uint32_t sky_top = 0xFF78C2FF;
    const uint32_t sky_bottom = 0xFF172433;
    std::unordered_set<uint32_t> colors;
    for (size_t y = 0; y < height; ++y)
    {
        const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom,
                                               engine.world.sky.exposure);
        for (size_t x = 0; x < width; ++x)
        {
            const uint32_t pixel = framebuffer[y * width + x];
            if (pixel != sky)
            {
                colors.insert(pixel);
            }
        }
    }

    REQUIRE(colors.size() >= 3);
}

TEST_CASE("default terrain palette differentiates material response")
{
    const TerrainConfig config;
    REQUIRE(config.palette.size() > std::max({config.stone, config.dirt,
                                              config.grass, config.water}));

    const Material& stone = config.palette[config.stone];
    const Material& dirt = config.palette[config.dirt];
    const Material& grass = config.palette[config.grass];
    const Material& water = config.palette[config.water];

    REQUIRE(water.shininess > stone.shininess);
    REQUIRE(stone.shininess > grass.shininess);
    REQUIRE(water.specular > stone.specular);
    REQUIRE(stone.specular > grass.specular);
    REQUIRE(grass.specular > dirt.specular);
    REQUIRE(grass.diffuse > water.diffuse);
}

TEST_CASE("RenderEngine update applies lighting as multiple shades")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 160;
    const size_t height = 120;

    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.settings.paused = true;
    engine.world.sun.direction = {0.5, 1.0, 0.7};
    engine.world.sun.intensity = 1.0;
    engine.world.sky.sky_light_scale = 1.0;

    engine.update(framebuffer.data(), width, height);

    const uint32_t sky_top = 0xFF78C2FF;
    const uint32_t sky_bottom = 0xFF172433;
    std::unordered_set<uint32_t> colors;
    for (size_t y = 0; y < height; ++y)
    {
        const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom,
                                               engine.world.sky.exposure);
        for (size_t x = 0; x < width; ++x)
        {
            const uint32_t pixel = framebuffer[y * width + x];
            if (pixel != sky)
            {
                colors.insert(pixel);
            }
        }
    }

    engine.settings.paused = false;

    REQUIRE(colors.size() >= 2);
}

TEST_CASE("light direction magnitude does not affect lighting")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.world.sun.orbit_enabled = false;
    engine.settings.shadow_enabled = false;
    engine.settings.taa.enabled = false;
    engine.settings.gi.enabled = false;
    engine.settings.gi.strength = 0.0;
    engine.world.sky.sky_light_scale = 0.0;
    engine.settings.ambient_occlusion_enabled = false;
    engine.settings.paused = true;
    engine.world.sun.intensity = 1.0;

    const size_t width = 120;
    const size_t height = 80;
    std::vector<uint32_t> frame_a(width * height, 0u);
    std::vector<uint32_t> frame_b(width * height, 0u);

    engine.world.sun.direction = {0.3, 0.7, 0.2};
    engine.update(frame_a.data(), width, height);

    engine.world.sun.direction = {0.6, 1.4, 0.4};
    engine.update(frame_b.data(), width, height);

    REQUIRE(frame_a == frame_b);
}

TEST_CASE("render state setters and getters")
{
    RenderEngine engine;
    reset_camera(engine);

    engine.world.sun.direction = {0.1, 0.2, -0.3};
    const Vec3 dir = engine.world.sun.direction;
    REQUIRE(dir.x == Catch::Approx(0.1));
    REQUIRE(dir.y == Catch::Approx(0.2));
    REQUIRE(dir.z == Catch::Approx(-0.3));

    engine.world.sun.intensity = 0.42;
    REQUIRE(engine.world.sun.intensity == Catch::Approx(0.42));

    engine.world.sun.orbit_enabled = true;
    REQUIRE(engine.world.sun.orbit_enabled);
    engine.world.sun.orbit_enabled = false;
    REQUIRE_FALSE(engine.world.sun.orbit_enabled);

    engine.world.sun.orbit_angle = 1.23;
    REQUIRE(engine.world.sun.orbit_angle == Catch::Approx(1.23));

    engine.world.sky.day_zenith = ColorSrgb::from_hex(0xFF112233).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(0xFF445566).to_linear();
    REQUIRE(pack_color(engine.world.sky.day_zenith.to_srgb()) == 0xFF112233);
    REQUIRE(pack_color(engine.world.sky.day_horizon.to_srgb()) == 0xFF445566);

    engine.world.sky.sky_light_scale = 0.77;
    REQUIRE(engine.world.sky.sky_light_scale == Catch::Approx(0.77));

    engine.world.sky.exposure = std::max(0.0, 1.4);
    REQUIRE(engine.world.sky.exposure == Catch::Approx(1.4));

    engine.settings.taa.enabled = true;
    REQUIRE(engine.settings.taa.enabled);
    engine.settings.taa.enabled = false;
    REQUIRE_FALSE(engine.settings.taa.enabled);

    engine.settings.taa.blend = 0.25;
    REQUIRE(engine.settings.taa.blend == Catch::Approx(0.25));

    engine.settings.gi.enabled = true;
    REQUIRE(engine.settings.gi.enabled);
    engine.settings.gi.enabled = false;
    REQUIRE_FALSE(engine.settings.gi.enabled);

    engine.settings.gi.strength = 0.6;
    REQUIRE(engine.settings.gi.strength == Catch::Approx(0.6));

    engine.settings.gi.bounce_count = 2;
    REQUIRE(engine.settings.gi.bounce_count == 2);
    engine.settings.gi.bounce_count = 0;
    REQUIRE(engine.settings.gi.bounce_count == 0);
    engine.settings.gi.bounce_count = 1;

    engine.settings.paused = false;
    REQUIRE_FALSE(engine.settings.paused);
    engine.settings.paused = !engine.settings.paused;
    REQUIRE(engine.settings.paused);
    engine.settings.paused = !engine.settings.paused;
    REQUIRE_FALSE(engine.settings.paused);

    engine.world.sun.direction = {0.0, 0.0, -1.0};
    engine.world.sun.intensity = 1.0;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(0xFF78C2FF).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(0xFF172433).to_linear();
    engine.world.sky.sky_light_scale = 0.0;
    engine.world.sky.exposure = std::max(0.0, 1.0);
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.2;
    engine.settings.paused = false;
}

TEST_CASE("terrain mesh culls internal faces")
{
    Terrain terrain;
    const int chunk_size = 16;
    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    auto count_blocks = [chunk_size](const std::vector<int>& heights) {
        size_t total = 0;
        for (int z = 0; z < chunk_size; ++z)
        {
            for (int x = 0; x < chunk_size; ++x)
            {
                total += static_cast<size_t>(heights[static_cast<size_t>(z * chunk_size + x)]);
            }
        }
        return total;
    };

    auto count_visible_faces = [chunk_size](const std::vector<int>& heights) {
        size_t faces = 0;
        for (int z = 0; z < chunk_size; ++z)
        {
            for (int x = 0; x < chunk_size; ++x)
            {
                const int height = heights[static_cast<size_t>(z * chunk_size + x)];
                for (int y = 0; y < height; ++y)
                {
                    if (!terrain_has_block(heights, chunk_size, x + 1, y, z)) faces++;
                    if (!terrain_has_block(heights, chunk_size, x - 1, y, z)) faces++;
                    if (!terrain_has_block(heights, chunk_size, x, y + 1, z)) faces++;
                    if (!terrain_has_block(heights, chunk_size, x, y - 1, z)) faces++;
                    if (!terrain_has_block(heights, chunk_size, x, y, z + 1)) faces++;
                    if (!terrain_has_block(heights, chunk_size, x, y, z - 1)) faces++;
                }
            }
        }
        return faces;
    };

    const size_t total_blocks = count_blocks(heights);
    const size_t expected_faces = count_visible_faces(heights);

    terrain.generate();
    REQUIRE(terrain.blocks.size() == total_blocks);
    REQUIRE(terrain.visible_faces == expected_faces);
    REQUIRE(terrain.mesh.size() * 2 < total_blocks * 12);
}

TEST_CASE("RenderEngine update renders sky gradient")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.position = {0.0, -25.0, -10.0};
    engine.camera.set_rotation({0.0, 0.8});
    engine.settings.paused = true;
    engine.world.sun.direction = {0.0, 0.0, 1.0};
    engine.world.sun.intensity = 0.0;

    const uint32_t sky_top = 0xFFFF4D4D;
    const uint32_t sky_bottom = 0xFF2A1B70;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_bottom).to_linear();
    engine.world.sky.sky_light_scale = 0.0;

    const size_t width = 120;
    const size_t height = 80;
    std::vector<uint32_t> framebuffer(width * height, 0u);
    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;

    const uint32_t top_color = sky_row_color(0, height, sky_top, sky_bottom,
                                                 engine.world.sky.exposure);
    const size_t mid = height / 2;
    const uint32_t mid_color = sky_row_color(mid, height, sky_top, sky_bottom,
                                                 engine.world.sky.exposure);

    bool found_top = false;
    for (size_t x = 0; x < width; ++x)
    {
        if (framebuffer[x] == top_color)
        {
            found_top = true;
            break;
        }
    }
    REQUIRE(found_top);

    bool found_mid = false;
    for (size_t x = 0; x < width; ++x)
    {
        if (framebuffer[mid * width + x] == mid_color)
        {
            found_mid = true;
            break;
        }
    }
    REQUIRE(found_mid);
}

TEST_CASE("sky gradient follows sun altitude when orbit enabled")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.position = {0.0, -25.0, -10.0};
    engine.camera.set_rotation({0.0, 0.8});
    engine.world.sun.direction = {0.0, 0.0, 1.0};
    engine.world.sun.intensity = 1.0;
    engine.world.sky.sky_light_scale = 0.0;

    const uint32_t noon_top = 0xFF78C2FF;
    const uint32_t noon_bottom = 0xFF172433;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(noon_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(noon_bottom).to_linear();

    const uint32_t sunrise_top = 0xFFB55A1A;
    const uint32_t sunrise_bottom = 0xFF4A200A;
    engine.world.sky.dawn_zenith = ColorSrgb::from_hex(sunrise_top).to_linear();
    engine.world.sky.dawn_horizon = ColorSrgb::from_hex(sunrise_bottom).to_linear();

    engine.world.sun.orbit_enabled = true;
    engine.settings.paused = true;
    engine.settings.taa.enabled = false;

    const size_t width = 120;
    const size_t height = 80;
    std::vector<uint32_t> sunrise_frame(width * height, 0u);
    std::vector<uint32_t> noon_frame(width * height, 0u);

    engine.world.sun.orbit_angle = 0.0;
    engine.update(sunrise_frame.data(), width, height);

    engine.world.sun.orbit_angle = 1.5707963267948966;
    engine.update(noon_frame.data(), width, height);

    engine.settings.paused = false;
    engine.world.sun.orbit_enabled = false;

    const uint32_t sunrise_top_row = sky_row_color(0, height, sunrise_top, sunrise_bottom,
                                                       engine.world.sky.exposure);
    const uint32_t noon_top_row = sky_row_color(0, height, noon_top, noon_bottom,
                                                    engine.world.sky.exposure);

    bool found_sunrise = false;
    bool found_noon = false;
    for (size_t x = 0; x < width; ++x)
    {
        if (sunrise_frame[x] == sunrise_top_row)
        {
            found_sunrise = true;
        }
        if (noon_frame[x] == noon_top_row)
        {
            found_noon = true;
        }
    }
    REQUIRE(sunrise_top_row != noon_top_row);
    REQUIRE(found_sunrise);
    REQUIRE(found_noon);
}

TEST_CASE("atmosphere supports blue hour and night gradients")
{
    Skybox sky{};
    sky.day_zenith = ColorSrgb::from_hex(0xFF112233).to_linear();
    sky.day_horizon = ColorSrgb::from_hex(0xFF223344).to_linear();
    sky.dawn_zenith = ColorSrgb::from_hex(0xFFAA5500).to_linear();
    sky.dawn_horizon = ColorSrgb::from_hex(0xFF552200).to_linear();
    sky.golden_zenith = ColorSrgb::from_hex(0xFFCC7A33).to_linear();
    sky.golden_horizon = ColorSrgb::from_hex(0xFF7A3A11).to_linear();
    sky.blue_zenith = ColorSrgb::from_hex(0xFF0A1A3A).to_linear();
    sky.blue_horizon = ColorSrgb::from_hex(0xFF0F2348).to_linear();
    sky.night_zenith = ColorSrgb::from_hex(0xFF02030A).to_linear();
    sky.night_horizon = ColorSrgb::from_hex(0xFF04060F).to_linear();
    sky.golden_height = 0.2;
    sky.golden_end = 0.08;
    sky.blue_height = -0.1;
    sky.night_height = -0.4;

    auto [zenith_day, horizon_day] = sky.sample(1.0f);
    REQUIRE(zenith_day.r == Catch::Approx(sky.day_zenith.r).margin(1e-6));
    REQUIRE(zenith_day.g == Catch::Approx(sky.day_zenith.g).margin(1e-6));
    REQUIRE(zenith_day.b == Catch::Approx(sky.day_zenith.b).margin(1e-6));
    REQUIRE(horizon_day.r == Catch::Approx(sky.day_horizon.r).margin(1e-6));
    REQUIRE(horizon_day.g == Catch::Approx(sky.day_horizon.g).margin(1e-6));
    REQUIRE(horizon_day.b == Catch::Approx(sky.day_horizon.b).margin(1e-6));

    auto [zenith_dawn, horizon_dawn] = sky.sample(0.0f);
    REQUIRE(zenith_dawn.r == Catch::Approx(sky.dawn_zenith.r).margin(1e-6));
    REQUIRE(zenith_dawn.g == Catch::Approx(sky.dawn_zenith.g).margin(1e-6));
    REQUIRE(zenith_dawn.b == Catch::Approx(sky.dawn_zenith.b).margin(1e-6));
    REQUIRE(horizon_dawn.r == Catch::Approx(sky.dawn_horizon.r).margin(1e-6));
    REQUIRE(horizon_dawn.g == Catch::Approx(sky.dawn_horizon.g).margin(1e-6));
    REQUIRE(horizon_dawn.b == Catch::Approx(sky.dawn_horizon.b).margin(1e-6));

    auto [zenith_blue, horizon_blue] = sky.sample(static_cast<float>(sky.blue_height));
    REQUIRE(zenith_blue.r == Catch::Approx(sky.blue_zenith.r).margin(1e-6));
    REQUIRE(zenith_blue.g == Catch::Approx(sky.blue_zenith.g).margin(1e-6));
    REQUIRE(zenith_blue.b == Catch::Approx(sky.blue_zenith.b).margin(1e-6));
    REQUIRE(horizon_blue.r == Catch::Approx(sky.blue_horizon.r).margin(1e-6));
    REQUIRE(horizon_blue.g == Catch::Approx(sky.blue_horizon.g).margin(1e-6));
    REQUIRE(horizon_blue.b == Catch::Approx(sky.blue_horizon.b).margin(1e-6));

    auto [zenith_night, horizon_night] = sky.sample(static_cast<float>(sky.night_height));
    REQUIRE(zenith_night.r == Catch::Approx(sky.night_zenith.r).margin(1e-6));
    REQUIRE(zenith_night.g == Catch::Approx(sky.night_zenith.g).margin(1e-6));
    REQUIRE(zenith_night.b == Catch::Approx(sky.night_zenith.b).margin(1e-6));
    REQUIRE(horizon_night.r == Catch::Approx(sky.night_horizon.r).margin(1e-6));
    REQUIRE(horizon_night.g == Catch::Approx(sky.night_horizon.g).margin(1e-6));
    REQUIRE(horizon_night.b == Catch::Approx(sky.night_horizon.b).margin(1e-6));
}

TEST_CASE("atmosphere blends through golden hour")
{
    Skybox sky{};
    sky.day_zenith = ColorSrgb::from_hex(0xFF102030).to_linear();
    sky.day_horizon = ColorSrgb::from_hex(0xFF203040).to_linear();
    sky.golden_zenith = ColorSrgb::from_hex(0xFFCC8844).to_linear();
    sky.golden_horizon = ColorSrgb::from_hex(0xFF885522).to_linear();
    sky.dawn_zenith = ColorSrgb::from_hex(0xFFAA5500).to_linear();
    sky.dawn_horizon = ColorSrgb::from_hex(0xFF552200).to_linear();
    sky.golden_height = 0.4;
    sky.golden_end = 0.1;

    auto [zenith_day, horizon_day] = sky.sample(static_cast<float>(sky.golden_height));
    REQUIRE(zenith_day.r == Catch::Approx(sky.day_zenith.r).margin(1e-6));
    REQUIRE(horizon_day.b == Catch::Approx(sky.day_horizon.b).margin(1e-6));

    auto [zenith_golden, horizon_golden] = sky.sample(static_cast<float>(sky.golden_end));
    REQUIRE(zenith_golden.r == Catch::Approx(sky.golden_zenith.r).margin(1e-6));
    REQUIRE(horizon_golden.g == Catch::Approx(sky.golden_horizon.g).margin(1e-6));

    auto [zenith_dawn, horizon_dawn] = sky.sample(0.0f);
    REQUIRE(zenith_dawn.r == Catch::Approx(sky.dawn_zenith.r).margin(1e-6));
    REQUIRE(horizon_dawn.g == Catch::Approx(sky.dawn_horizon.g).margin(1e-6));

    const float mid_h = 0.25f;
    auto smoothstep = [](float edge0, float edge1, float x) -> float {
        if (edge0 == edge1)
        {
            return x < edge0 ? 0.0f : 1.0f;
        }
        const float tt = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return tt * tt * (3.0f - 2.0f * tt);
    };
    const float t = smoothstep(static_cast<float>(sky.golden_end),
                               static_cast<float>(sky.golden_height),
                               mid_h);
    const LinearColor expected_zenith = LinearColor::lerp(sky.golden_zenith, sky.day_zenith, t);
    const LinearColor expected_horizon = LinearColor::lerp(sky.golden_horizon, sky.day_horizon, t);
    auto [zenith_mid, horizon_mid] = sky.sample(mid_h);
    REQUIRE(zenith_mid.r == Catch::Approx(expected_zenith.r).margin(1e-6));
    REQUIRE(zenith_mid.g == Catch::Approx(expected_zenith.g).margin(1e-6));
    REQUIRE(zenith_mid.b == Catch::Approx(expected_zenith.b).margin(1e-6));
    REQUIRE(horizon_mid.r == Catch::Approx(expected_horizon.r).margin(1e-6));
    REQUIRE(horizon_mid.g == Catch::Approx(expected_horizon.g).margin(1e-6));
    REQUIRE(horizon_mid.b == Catch::Approx(expected_horizon.b).margin(1e-6));
}

TEST_CASE("atmosphere intensity falls after sunset and moon lifts night")
{
    Skybox sky{};
    sky.sky_light_scale = 1.0;
    sky.dusk_light_ratio = 0.4;
    sky.blue_hour_light_ratio = 0.2;
    sky.night_light_ratio = 0.05;
    sky.midnight_light_ratio = 0.05;
    sky.golden_height = 0.1;
    sky.blue_height = -0.1;
    sky.night_height = -0.3;
    sky.moon_ambient_floor = 0.25;

    const float day = sky.intensity(1.0f, 0.0f);
    const float dusk = sky.intensity(0.0f, 0.0f);
    const float blue = sky.intensity(static_cast<float>(sky.blue_height), 0.0f);
    const float night = sky.intensity(-1.0f, 0.0f);
    REQUIRE(day > dusk);
    REQUIRE(dusk > blue);
    REQUIRE(blue > night);

    const float night_moon = sky.intensity(-1.0f, 1.0f);
    REQUIRE(night_moon > night);
}

TEST_CASE("sun orbit spans below the horizon")
{
    World world{};
    world.sun.night_length_ratio = 0.25;
    const double night_ratio = std::max(0.0, world.sun.night_length_ratio);
    const double total = 1.0 + night_ratio;
    const double day_fraction = 1.0 / total;
    const double night_fraction = night_ratio / total;
    const double angle = Celestial::kTau * (day_fraction + night_fraction * 0.5);
    const Vec3 dir = world.sun.direction_at(angle);
    const double signed_height = world.sun.height_signed(dir);
    REQUIRE(signed_height < 0.0);
}

TEST_CASE("sun orbit supports night length ratio")
{
    World world{};
    world.sun.night_length_ratio = 0.25;
    const int samples = 2000;
    int day_count = 0;
    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(samples);
        const double angle = Celestial::kTau * t;
        const Vec3 dir = world.sun.direction_at(angle);
        if (world.sun.height_signed(dir) > 0.0)
        {
            day_count++;
        }
    }
    const double day_fraction = static_cast<double>(day_count) / static_cast<double>(samples);
    REQUIRE(day_fraction == Catch::Approx(0.8).margin(0.05));
}

TEST_CASE("sky light intensity follows sun altitude")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.position = {0.0, 12.0, -10.0};
    engine.camera.set_rotation({0.0, 0.7});
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 0.0;
    engine.settings.shadow_enabled = false;

    const uint32_t sunrise_top = 0xFFB55A1A;
    const uint32_t sunrise_bottom = 0xFF4A200A;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sunrise_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sunrise_bottom).to_linear();
    engine.world.sky.golden_zenith = engine.world.sky.day_zenith;
    engine.world.sky.golden_horizon = engine.world.sky.day_horizon;
    engine.world.sky.dawn_zenith = engine.world.sky.day_zenith;
    engine.world.sky.dawn_horizon = engine.world.sky.day_horizon;
    engine.world.sky.sky_light_scale = 1.0;

    engine.world.sun.orbit_enabled = true;
    engine.settings.paused = true;

    const size_t width = 160;
    const size_t height = 120;
    std::vector<uint32_t> sunrise_frame(width * height, 0u);
    std::vector<uint32_t> noon_frame(width * height, 0u);

    engine.world.sun.orbit_angle = 0.0;
    engine.update(sunrise_frame.data(), width, height);

    engine.world.sun.orbit_angle = 1.5707963267948966;
    engine.update(noon_frame.data(), width, height);

    engine.settings.paused = false;
    engine.world.sun.orbit_enabled = false;

    double sum_sunrise = 0.0;
    double sum_noon = 0.0;
    size_t count = 0;
    for (size_t y = 0; y < height; ++y)
    {
        const uint32_t sky = sky_row_color(y, height, sunrise_top, sunrise_bottom,
                                               engine.world.sky.exposure);
        for (size_t x = 0; x < width; ++x)
        {
            const size_t idx = y * width + x;
            if (sunrise_frame[idx] == sky || noon_frame[idx] == sky)
            {
                continue;
            }
            sum_sunrise += static_cast<double>(luminance(sunrise_frame[idx]));
            sum_noon += static_cast<double>(luminance(noon_frame[idx]));
            count++;
        }
    }

    REQUIRE(count > 0);
    REQUIRE(sum_noon > sum_sunrise + 50.0);
}

TEST_CASE("low sun altitude keeps ambient above black")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = true;
    engine.world.sun.orbit_angle = 0.08;
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 0.0;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;

    const uint32_t sky_color = 0xFFFFFFFF;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_color).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_color).to_linear();
    engine.world.sky.sky_light_scale = 1.0;

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> framebuffer(width * height, 0u);
    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;
    engine.world.sun.orbit_enabled = false;

    const Vec2 projected = project_point(engine.camera, {center_x, center_y + 1.0, center_z}, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double avg_lum = 0.0;
    REQUIRE(average_luminance(framebuffer, width, height, px, py, sky_color, sky_color,
                                     engine.world.sky.exposure, avg_lum));
    REQUIRE(avg_lum > 30.0);
}

TEST_CASE("sun light is warmer than moon light")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = false;
    engine.world.sun.orbit_enabled = false;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;
    engine.world.sky.sky_light_scale = 0.0;

    const uint32_t sky_color = 0xFF010203;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_color).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_color).to_linear();

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const Vec3 sample_point{center_x, center_y + 1.0, center_z};

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> sun_frame(width * height, 0u);
    std::vector<uint32_t> moon_frame(width * height, 0u);

    engine.world.sun.direction = {0.0, 1.0, 0.0};
    engine.world.sun.intensity = 1.0;
    engine.world.moon.intensity = 0.0;
    engine.update(sun_frame.data(), width, height);

    engine.world.sun.direction = {0.0, -1.0, 0.0};
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 1.0;
    engine.update(moon_frame.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, sample_point, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double r_sun = 0.0;
    double g_sun = 0.0;
    double b_sun = 0.0;
    double r_moon = 0.0;
    double g_moon = 0.0;
    double b_moon = 0.0;
    REQUIRE(average_color(sun_frame, width, height, px, py, sky_color, sky_color,
                                 engine.world.sky.exposure, r_sun, g_sun, b_sun));
    REQUIRE(average_color(moon_frame, width, height, px, py, sky_color, sky_color,
                                 engine.world.sky.exposure, r_moon, g_moon, b_moon));

    REQUIRE(b_sun > 1.0);
    REQUIRE(b_moon > 1.0);
    const double ratio_sun = r_sun / b_sun;
    const double ratio_moon = r_moon / b_moon;
    REQUIRE(ratio_sun > ratio_moon + 0.05);
}

TEST_CASE("moonlight adds ambient when sun is below horizon")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = true;
    engine.world.sun.night_length_ratio = 0.25;
    const double night_ratio = std::max(0.0, engine.world.sun.night_length_ratio);
    const double total = 1.0 + night_ratio;
    const double day_fraction = 1.0 / total;
    const double night_fraction = night_ratio / total;
    engine.world.sun.orbit_angle = Celestial::kTau * (day_fraction + night_fraction * 0.5);
    engine.world.sun.intensity = 0.0;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;
    engine.world.sky.sky_light_scale = 1.0;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(0xFFFFFFFF).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(0xFFFFFFFF).to_linear();

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> moon_on(width * height, 0u);
    std::vector<uint32_t> moon_off(width * height, 0u);

    engine.world.moon.intensity = 0.6;
    engine.update(moon_on.data(), width, height);
    engine.world.moon.intensity = 0.0;
    engine.update(moon_off.data(), width, height);
    engine.settings.paused = false;
    engine.world.sun.orbit_enabled = false;

    const Vec2 projected = project_point(engine.camera, {center_x, center_y + 1.0, center_z}, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double avg_on = 0.0;
    double avg_off = 0.0;
    REQUIRE(average_luminance(moon_on, width, height, px, py, 0xFFFFFFFF, 0xFFFFFFFF,
                                     engine.world.sky.exposure, avg_on));
    REQUIRE(average_luminance(moon_off, width, height, px, py, 0xFFFFFFFF, 0xFFFFFFFF,
                                     engine.world.sky.exposure, avg_off));
    REQUIRE(avg_on > avg_off + 8.0);
}

TEST_CASE("sky light increases ambient brightness")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.direction = {0.0, 0.0, 1.0};
    engine.world.sun.intensity = 0.0;

    const uint32_t sky_top = 0xFF9AD4FF;
    const uint32_t sky_bottom = 0xFF101820;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_bottom).to_linear();

    const size_t width = 120;
    const size_t height = 80;
    std::vector<uint32_t> no_sky_light(width * height, 0u);
    std::vector<uint32_t> with_sky_light(width * height, 0u);

    engine.world.sky.sky_light_scale = 0.0;
    engine.update(no_sky_light.data(), width, height);

    engine.world.sky.sky_light_scale = 1.0;
    engine.update(with_sky_light.data(), width, height);
    engine.settings.paused = false;

    double sum_no = 0.0;
    double sum_with = 0.0;
    size_t count = 0;
    for (size_t y = 0; y < height; ++y)
    {
        const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom,
                                               engine.world.sky.exposure);
        for (size_t x = 0; x < width; ++x)
        {
            const size_t idx = y * width + x;
            if (no_sky_light[idx] == sky)
            {
                continue;
            }
            sum_no += static_cast<double>(luminance(no_sky_light[idx]));
            sum_with += static_cast<double>(luminance(with_sky_light[idx]));
            count++;
        }
    }

    REQUIRE(count > 0);
    REQUIRE(sum_with > sum_no);
}

TEST_CASE("ambient light applies without direct lighting")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = false;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 0.0;
    engine.world.sky.sky_light_scale = 0.0;

    const uint32_t sky_color = 0xFF101010;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_color).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_color).to_linear();

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> ambient_off(width * height, 0u);
    std::vector<uint32_t> ambient_on(width * height, 0u);

    engine.settings.ambient_light = 0.0;
    engine.update(ambient_off.data(), width, height);
    engine.settings.ambient_light = 0.35;
    engine.update(ambient_on.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, {center_x, center_y + 1.0, center_z}, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double avg_off = 0.0;
    double avg_on = 0.0;
    REQUIRE(average_luminance(ambient_off, width, height, px, py, sky_color, sky_color,
                                     engine.world.sky.exposure, avg_off));
    REQUIRE(average_luminance(ambient_on, width, height, px, py, sky_color, sky_color,
                                     engine.world.sky.exposure, avg_on));
    REQUIRE(avg_on > avg_off + 3.0);
}

TEST_CASE("stars add variation to night sky")
{
    RenderBuffers buffers;
    PostProcessor post;
    const size_t width = 128;
    const size_t height = 96;
    const float depth_max = std::numeric_limits<float>::max();
    buffers.resize(width, height, depth_max);
    post.resize_buffers(width * height);

    const uint32_t sky_hex = 0xFF0A0C18;
    const LinearColor sky = ColorSrgb::from_hex(sky_hex).to_linear();
    Skybox skybox{};
    std::vector<uint32_t> no_stars(width * height, 0u);
    std::vector<uint32_t> with_stars(width * height, 0u);
    GiSettings gi{};
    FrameLighting no_star_lighting{
        .sky_zenith = sky,
        .sky_horizon = sky
    };
    FrameLighting star_lighting = no_star_lighting;
    star_lighting.star_visibility = 1.0f;

    post.resolve_frame(no_stars.data(), buffers, PostFrame{
        .lighting = no_star_lighting
    }, skybox, gi);
    post.resolve_frame(with_stars.data(), buffers, PostFrame{
        .lighting = star_lighting
    }, skybox, gi);

    size_t diff = 0;
    for (size_t i = 0; i < no_stars.size(); ++i)
    {
        if (with_stars[i] != no_stars[i])
        {
            diff++;
        }
    }
    const size_t min_diff = (width * height) / 400;
    REQUIRE(diff > min_diff);
}

TEST_CASE("celestial disks include an analytic glow")
{
    RenderBuffers buffers;
    PostProcessor post;
    constexpr size_t width = 128;
    constexpr size_t height = 96;
    buffers.resize(width, height, std::numeric_limits<float>::max());
    post.resize_buffers(width * height);

    Camera camera;
    camera.position = {0.0, 0.0, 0.0};
    camera.set_rotation({0.0, 0.0});
    const double proj_scale = static_cast<double>(height) * Camera::fov_scale;
    const Mat4 vp = Camera::projection(width, height, proj_scale, proj_scale)
                    * camera.view_matrix();
    post.inverseCurrentVP = vp.invert().value_or(Mat4::identity());

    const LinearColor sky = ColorSrgb::from_hex(0xFF080C18).to_linear();
    FrameLighting base_lighting{
        .sky_zenith = sky,
        .sky_horizon = sky
    };
    FrameLighting disk_lighting = base_lighting;
    disk_lighting.lights[0] = {
        .dir = {0.0, 0.0, 1.0},
        .color = {1.0f, 0.8f, 0.55f},
        .disk = {.radius = 0.05, .radiance = 4.0}
    };

    Skybox skybox;
    GiSettings gi;
    std::vector<uint32_t> base(width * height);
    std::vector<uint32_t> with_disk(width * height);
    post.resolve_frame(base.data(), buffers, PostFrame{
        .lighting = base_lighting,
        .camera_pos = camera.position
    }, skybox, gi);
    post.resolve_frame(with_disk.data(), buffers, PostFrame{
        .lighting = disk_lighting,
        .camera_pos = camera.position
    }, skybox, gi);

    constexpr size_t center_x = width / 2;
    constexpr size_t center_y = height / 2;
    constexpr size_t glow_x = center_x + 12;
    constexpr size_t far_x = center_x + 40;
    const size_t center = center_y * width + center_x;
    const size_t glow = center_y * width + glow_x;
    const size_t far = center_y * width + far_x;

    REQUIRE(luminance(with_disk[center]) > luminance(with_disk[glow]));
    REQUIRE(luminance(with_disk[glow]) > luminance(base[glow]));
    REQUIRE(with_disk[far] == base[far]);
}

TEST_CASE("celestial disk color follows sun altitude and keeps the moon cool")
{
    World world;
    world.sun.orbit_enabled = false;

    world.sun.direction = {0.0, 0.05, 1.0};
    const FrameLighting low_sun = world.evaluate_lighting(1.0);
    const LinearColor warm = low_sun.lights[0].disk.color;
    REQUIRE(warm.r > warm.g);
    REQUIRE(warm.g > warm.b);

    world.sun.direction = {0.0, 1.0, 0.0};
    const FrameLighting day = world.evaluate_lighting(1.0);
    REQUIRE(day.lights[0].disk.radiance > 0.0);
    REQUIRE(day.lights[1].disk.radiance == Catch::Approx(0.0));
    const LinearColor noon = day.lights[0].disk.color;
    REQUIRE(noon.r == Catch::Approx(1.0f));
    REQUIRE(noon.g == Catch::Approx(1.0f));
    REQUIRE(noon.b == Catch::Approx(1.0f));

    world.sun.direction = {0.0, -1.0, 0.0};
    const FrameLighting night = world.evaluate_lighting(1.0);
    REQUIRE(night.lights[0].disk.radiance == Catch::Approx(0.0));
    REQUIRE(night.lights[1].disk.radiance > 0.0);
    const LinearColor moon = night.lights[1].disk.color;
    REQUIRE(moon.b > moon.g);
    REQUIRE(moon.g > moon.r);
    REQUIRE(moon.b - moon.r < 0.2f);
}

TEST_CASE("gamma correction applies to midtone ambient")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 0.0;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;

    const uint32_t sky_color = 0xFFFFFFFF;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_color).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_color).to_linear();
    engine.world.sky.sky_light_scale = 0.5;

    auto expected_gamma_luminance = [](uint32_t albedo, float intensity, double exposure) {
        const float r_lin = srgb_to_linear(static_cast<float>((albedo >> 16) & 0xFF)) * intensity;
        const float g_lin = srgb_to_linear(static_cast<float>((albedo >> 8) & 0xFF)) * intensity;
        const float b_lin = srgb_to_linear(static_cast<float>(albedo & 0xFF)) * intensity;
        const float exposure_f = static_cast<float>(std::max(0.0, exposure));
        const float r_mapped = exposure_f <= 0.0f ? 0.0f : (r_lin * exposure_f) / (1.0f + r_lin * exposure_f);
        const float g_mapped = exposure_f <= 0.0f ? 0.0f : (g_lin * exposure_f) / (1.0f + g_lin * exposure_f);
        const float b_mapped = exposure_f <= 0.0f ? 0.0f : (b_lin * exposure_f) / (1.0f + b_lin * exposure_f);
        const uint32_t r = static_cast<uint32_t>(std::lround(linear_to_srgb(r_mapped) * 255.0f));
        const uint32_t g = static_cast<uint32_t>(std::lround(linear_to_srgb(g_mapped) * 255.0f));
        const uint32_t b = static_cast<uint32_t>(std::lround(linear_to_srgb(b_mapped) * 255.0f));
        return r + g + b;
    };

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> framebuffer(width * height, 0u);
    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, {center_x, center_y + 1.0, center_z}, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double avg_lum = 0.0;
    REQUIRE(average_luminance(framebuffer, width, height, px, py, sky_color, sky_color,
                                     engine.world.sky.exposure, avg_lum));

    const uint32_t expected = expected_gamma_luminance(0xFF3B8A38, 0.5f, engine.world.sky.exposure);
    REQUIRE(avg_lum == Catch::Approx(static_cast<double>(expected)).margin(12.0));
}

TEST_CASE("Reinhard tone mapping applies exposure in linear space")
{
    const Vec3 color{2.0, 0.5, 0.0};
    const Vec3 mapped = tonemap_vec3(color, 1.0);
    REQUIRE(mapped.x == Catch::Approx(2.0 / 3.0).margin(1e-6));
    REQUIRE(mapped.y == Catch::Approx(0.5 / 1.5).margin(1e-6));
    REQUIRE(mapped.z == Catch::Approx(0.0).margin(1e-9));

    const Vec3 mapped_boost = tonemap_vec3(color, 2.0);
    REQUIRE(mapped_boost.x == Catch::Approx(4.0 / 5.0).margin(1e-6));
    REQUIRE(mapped_boost.y == Catch::Approx(1.0 / 2.0).margin(1e-6));
    REQUIRE(mapped_boost.x > mapped.x);
    REQUIRE(mapped_boost.y > mapped.y);

    const Vec3 mapped_zero = tonemap_vec3(color, -1.0);
    REQUIRE(mapped_zero.x == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(mapped_zero.y == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(mapped_zero.z == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("hemisphere lighting adds sun bounce to shadowed faces")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.direction = {0.0, 1.0, 0.0};
    engine.world.moon.intensity = 0.0;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;
    engine.camera.position = {0.0, 10.0, -12.0};
    engine.camera.set_rotation({0.0, 0.6});

    const uint32_t sky_top = 0xFFFFFFFF;
    const uint32_t sky_bottom = 0xFF202020;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_bottom).to_linear();
    engine.world.sky.sky_light_scale = 1.0;

    auto find_front_column = [](int& out_x, int& out_height) {
        const int chunk_size = 16;
        std::vector<int> heights;
        std::vector<uint32_t> top_colors;
        build_heightmap(heights, top_colors);

        auto index = [chunk_size](int x, int z) {
            return static_cast<size_t>(z * chunk_size + x);
        };

        int best_height = -1;
        int best_x = 0;
        const int z = 0;
        for (int x = 0; x < chunk_size; ++x)
        {
            const int height = heights[index(x, z)];
            if (height > best_height)
            {
                best_height = height;
                best_x = x;
            }
        }

        if (best_height <= 0)
        {
            return false;
        }

        out_x = best_x;
        out_height = best_height;
        return true;
    };

    int column_x = 0;
    int column_height = 0;
    REQUIRE(find_front_column(column_x, column_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + column_x * block_size;
    const double center_y = base_y + (column_height - 1) * block_size;
    const double front_z = start_z - 1.0;
    const Vec3 probe{center_x, center_y, front_z};

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> sun_on(width * height, 0u);
    std::vector<uint32_t> sun_off(width * height, 0u);

    engine.world.sun.intensity = 1.0;
    engine.update(sun_on.data(), width, height);
    engine.world.sun.intensity = 0.0;
    engine.update(sun_off.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, probe, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double avg_on = 0.0;
    double avg_off = 0.0;
    REQUIRE(average_luminance(sun_on, width, height, px, py, sky_top, sky_bottom,
                                     engine.world.sky.exposure, avg_on));
    REQUIRE(average_luminance(sun_off, width, height, px, py, sky_top, sky_bottom,
                                     engine.world.sky.exposure, avg_off));
    REQUIRE(avg_on > avg_off + 3.0);
}

TEST_CASE("deferred shadow pass traces the final G-buffer samples")
{
    Terrain terrain;
    terrain.generate();
    LightingEngine lighting;
    RenderSettings settings;
    settings.shadow.filter_enabled = false;
    const Vec3 light_dir = normalize_vec({0.6, 0.3, 0.8});

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    Vec3 shadowed{};
    Vec3 lit{};
    REQUIRE(find_shadow_sample(heights, light_dir, {0.0, 1.0, 0.0}, true, &shadowed));
    REQUIRE(find_shadow_sample(heights, light_dir, {0.0, 1.0, 0.0}, false, &lit));

    const float depth_max = std::numeric_limits<float>::max();
    RenderBuffers buffers;
    buffers.resize(2, 1, depth_max);
    buffers.zbuffer = {1.0f, 1.0f};
    buffers.world_positions = {shadowed, lit};
    buffers.sample_normals = {{0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
    buffers.sample_direct_sun = {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};

    const DirectionalLight sun{
        .dir = light_dir,
        .intensity = 1.0,
        .color = {1.0f, 1.0f, 1.0f}
    };
    lighting.resolve_direct(DirectFrame{
        .depth_max = depth_max,
        .frame_index = 0,
        .shadows_on = true,
        .lights = {sun, {}}
    }, buffers, terrain, settings.shadow);

    REQUIRE(buffers.shadow_mask_sun[0] == Catch::Approx(0.0f));
    REQUIRE(buffers.shadow_mask_sun[1] == Catch::Approx(1.0f));
    REQUIRE(buffers.sample_direct[0].r == Catch::Approx(0.0f));
    REQUIRE(buffers.sample_direct[1].r == Catch::Approx(1.0f));
}

TEST_CASE("one-bounce GI does not darken shadowed terrain")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 1.0;
    const Vec3 light_dir = normalize_vec({0.6, 0.3, 0.8});
    engine.world.sun.direction = {-light_dir.x, -light_dir.y, -light_dir.z};
    engine.settings.shadow_enabled = true;
    engine.world.sky.sky_light_scale = 0.0;
    engine.settings.ambient_occlusion_enabled = false;
    engine.settings.taa.enabled = false;
    engine.settings.gi.enabled = false;
    engine.settings.gi.strength = 0.0;

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    Vec3 shadow_point{0.0, 0.0, 0.0};
    REQUIRE(find_shadow_sample(heights, light_dir, {0.0, 1.0, 0.0}, true, &shadow_point));

    const Vec3 eye{shadow_point.x, shadow_point.y + 18.0, shadow_point.z - 14.0};
    const Vec3 to_target{shadow_point.x - eye.x, shadow_point.y - eye.y, shadow_point.z - eye.z};
    const double yaw = std::atan2(to_target.x, to_target.z);
    const double dist_xz = std::sqrt(to_target.x * to_target.x + to_target.z * to_target.z);
    const double pitch = std::atan2(-to_target.y, dist_xz);
    engine.camera.position = eye;
    engine.camera.set_rotation({yaw, pitch});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> framebuffer(width * height, 0u);

    const Vec2 projected = project_point(engine.camera, shadow_point, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);
    const uint32_t sky_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t sky_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());

    auto measure_avg = [&](int frames, double& out_avg) {
        double sum = 0.0;
        for (int i = 0; i < frames; ++i)
        {
            engine.update(framebuffer.data(), width, height);
            double avg = 0.0;
            REQUIRE(average_luminance(framebuffer, width, height, px, py, sky_top, sky_bottom,
                                             engine.world.sky.exposure, avg));
            sum += avg;
        }
        out_avg = sum / static_cast<double>(frames);
    };

    double avg_off = 0.0;
    measure_avg(8, avg_off);

    engine.settings.gi.enabled = true;
    engine.settings.gi.strength = 1.5;

    double avg_on = 0.0;
    measure_avg(8, avg_on);

    engine.settings.gi.enabled = false;
    engine.settings.gi.strength = 0.0;
    engine.settings.paused = false;

    REQUIRE(avg_on >= avg_off - 0.5);
}

TEST_CASE("GI bounce count does not reduce indirect contribution")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 1.0;
    const Vec3 light_dir = normalize_vec({0.6, 0.3, 0.8});
    engine.world.sun.direction = {-light_dir.x, -light_dir.y, -light_dir.z};
    engine.settings.shadow_enabled = true;
    engine.world.sky.sky_light_scale = 0.0;
    engine.settings.ambient_occlusion_enabled = false;
    engine.settings.taa.enabled = false;
    engine.settings.gi.enabled = true;
    engine.settings.gi.strength = 2.0;

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    Vec3 shadow_point{0.0, 0.0, 0.0};
    REQUIRE(find_shadow_sample(heights, light_dir, {0.0, 1.0, 0.0}, true, &shadow_point));

    const Vec3 eye{shadow_point.x, shadow_point.y + 18.0, shadow_point.z - 14.0};
    const Vec3 to_target{shadow_point.x - eye.x, shadow_point.y - eye.y, shadow_point.z - eye.z};
    const double yaw = std::atan2(to_target.x, to_target.z);
    const double dist_xz = std::sqrt(to_target.x * to_target.x + to_target.z * to_target.z);
    const double pitch = std::atan2(-to_target.y, dist_xz);
    engine.camera.position = eye;
    engine.camera.set_rotation({yaw, pitch});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> framebuffer(width * height, 0u);

    const Vec2 projected = project_point(engine.camera, shadow_point, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);
    const uint32_t sky_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t sky_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());

    auto measure_luminance = [&](int bounces, double& out_avg) {
        engine.settings.gi.bounce_count = bounces;
        engine.renderFrameIndex = 121;
        engine.update(framebuffer.data(), width, height);
        double avg = 0.0;
        REQUIRE(average_luminance(framebuffer, width, height, px, py, sky_top, sky_bottom,
                                         engine.world.sky.exposure, avg));
        out_avg = avg;
    };

    double avg_one = 0.0;
    double avg_two = 0.0;
    measure_luminance(1, avg_one);
    measure_luminance(2, avg_two);

    engine.settings.gi.enabled = false;
    engine.settings.gi.strength = 0.0;
    engine.settings.gi.bounce_count = 1;
    engine.settings.paused = false;

    REQUIRE(avg_two >= avg_one - 0.1);
}

TEST_CASE("light direction updates are consistent")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.world.sun.orbit_enabled = false;

    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{-4.0, -5.0, -6.0};

    engine.world.sun.direction = a;
    Vec3 v = engine.world.sun.direction;
    REQUIRE(v.x == Catch::Approx(a.x));
    REQUIRE(v.y == Catch::Approx(a.y));
    REQUIRE(v.z == Catch::Approx(a.z));

    engine.world.sun.direction = b;
    v = engine.world.sun.direction;
    REQUIRE(v.x == Catch::Approx(b.x));
    REQUIRE(v.y == Catch::Approx(b.y));
    REQUIRE(v.z == Catch::Approx(b.z));
}

TEST_CASE("stochastic DDA varies across frames near shadow boundaries")
{
    Terrain terrain;
    LightingEngine lighting;
    RenderSettings settings;
    World world_state;
    world_state.sun.orbit_enabled = false;
    world_state.sun.direction = {0.6, 0.2, 0.7};
    world_state.sun.intensity = 1.0;
    const bool shadows_enabled = true;

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    const Vec3 normal{0.0, 1.0, 0.0};
    const Vec3 light_dir = normalize_vec({0.6, 0.2, 0.7});

    auto shadow_factor_frame = [&](const Vec3 world_pos, const Vec3 normal_dir,
                                   const Vec3 light_dir_local,
                                   const int pixel_x, const int pixel_y,
                                   const int frame, float* out_factor) -> bool {
        if (!out_factor)
        {
            return false;
        }
        if (terrain.blocks.empty())
        {
            terrain.generate();
        }
        if (!shadows_enabled)
        {
            *out_factor = 1.0f;
            return true;
        }

        const Vec3 dir = light_dir_local.normalize();

        auto [right, up, forward] = Vec3::get_basis(dir);
        (void)forward;

        const double scale = std::tan(world_state.sun.angular_radius);
        const Vec3 right_scaled = {right.x * scale, right.y * scale, right.z * scale};
        const Vec3 up_scaled    = {up.x * scale, up.y * scale, up.z * scale};

        const BlueNoise::Shift shift_u = BlueNoise::shift(frame, settings.shadow.sun_salt);
        const BlueNoise::Shift shift_v = BlueNoise::shift(frame, settings.shadow.sun_salt + 1);
        const ShadowJitter jitter{
            .right = right_scaled,
            .up = up_scaled,
            .shift_u = shift_u,
            .shift_v = shift_v
        };
        const Vec3 shadow_dir = jitter.direction(dir, pixel_x, pixel_y);

        *out_factor = lighting.shadow_factor(terrain, shadow_dir, world_pos, normal_dir.normalize(),
                                             settings.shadow);
        return true;
    };

    bool saw_variation = false;
    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    auto index = [chunk_size](int x, int z) {
        return static_cast<size_t>(z * chunk_size + x);
    };

    const std::array<double, 3> offsets = {-0.35, 0.0, 0.35};
    for (int z = 0; z < chunk_size && !saw_variation; z += 2)
    {
        for (int x = 0; x < chunk_size && !saw_variation; x += 2)
        {
            const int height = heights[index(x, z)];
            if (height <= 0)
            {
                continue;
            }
            const double center_y = base_y + (height - 1) * block_size;
            const double top_y = center_y + block_size * 0.5;
            for (double ox : offsets)
            {
                for (double oz : offsets)
                {
                    const Vec3 world_pos{
                        start_x + x * block_size + ox * block_size,
                        top_y,
                        start_z + z * block_size + oz * block_size
                    };
                    float min_factor = 1.0f;
                    float max_factor = 0.0f;
                    for (int frame = 0; frame < 24; ++frame)
                    {
                        float sample = 1.0f;
                        if (!shadow_factor_frame(world_pos, normal, light_dir,
                                                 x, z, frame, &sample))
                        {
                            continue;
                        }
                        min_factor = std::min(min_factor, sample);
                        max_factor = std::max(max_factor, sample);
                        if (min_factor < 0.05f && max_factor > 0.95f)
                        {
                            saw_variation = true;
                            break;
                        }
                    }
                    if (saw_variation) break;
                }
                if (saw_variation) break;
            }
        }
    }

    REQUIRE(saw_variation);
}

TEST_CASE("moon light contributes when sun is disabled")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.direction = {0.4, -1.0, -0.3};
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 0.8;
    engine.world.sky.sky_light_scale = 0.0;
    engine.settings.shadow_enabled = false;

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const size_t width = 160;
    const size_t height = 120;
    std::vector<uint32_t> moon_on(width * height, 0u);
    std::vector<uint32_t> moon_off(width * height, 0u);

    engine.update(moon_on.data(), width, height);

    engine.world.moon.intensity = 0.0;
    engine.update(moon_off.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, {center_x, center_y + 1.0, center_z}, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    const uint32_t sky_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t sky_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());
    double avg_on = 0.0;
    double avg_off = 0.0;
    REQUIRE(average_luminance(moon_on, width, height, px, py, sky_top, sky_bottom,
                                     engine.world.sky.exposure, avg_on));
    REQUIRE(average_luminance(moon_off, width, height, px, py, sky_top, sky_bottom,
                                     engine.world.sky.exposure, avg_off));
    REQUIRE(avg_on > avg_off + 2.0);
}

TEST_CASE("moon light is suppressed when sun is high")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = false;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;
    engine.settings.ambient_light = 0.0;
    engine.world.sun.orbit_enabled = false;
    engine.world.sun.direction = {0.0, 1.0, 0.0};
    engine.world.sun.intensity = 0.0;
    engine.world.moon.intensity = 1.0;
    engine.world.sky.sky_light_scale = 0.0;

    const size_t width = 160;
    const size_t height = 120;
    std::vector<uint32_t> moon_on(width * height, 0u);
    std::vector<uint32_t> moon_off(width * height, 0u);

    engine.update(moon_on.data(), width, height);
    engine.world.moon.intensity = 0.0;
    engine.update(moon_off.data(), width, height);
    engine.settings.paused = false;

    double sum_on = 0.0;
    double sum_off = 0.0;
    size_t count = 0;
    const uint32_t sky_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t sky_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());
    for (size_t y = 0; y < height; ++y)
    {
        const uint32_t sky = sky_row_color(y, height, sky_top, sky_bottom,
                                               engine.world.sky.exposure);
        for (size_t x = 0; x < width; ++x)
        {
            const size_t idx = y * width + x;
            if (moon_on[idx] == sky || moon_off[idx] == sky)
            {
                continue;
            }
            sum_on += static_cast<double>(luminance(moon_on[idx]));
            sum_off += static_cast<double>(luminance(moon_off[idx]));
            count++;
        }
    }

    REQUIRE(count > 0);
    REQUIRE(sum_on < sum_off + 1.0);
}

TEST_CASE("phong shading varies within a terrain top face")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = false;
    engine.world.sun.intensity = 0.0;
    engine.world.sky.sky_light_scale = 1.0;

    const uint32_t sky_top = 0xFFFFFFFF;
    const uint32_t sky_bottom = 0xFF000000;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_bottom).to_linear();

    auto colors_similar = [](uint32_t a, uint32_t b, int tolerance) {
        const int ar = static_cast<int>((a >> 16) & 0xFF);
        const int ag = static_cast<int>((a >> 8) & 0xFF);
        const int ab = static_cast<int>(a & 0xFF);
        const int br = static_cast<int>((b >> 16) & 0xFF);
        const int bg = static_cast<int>((b >> 8) & 0xFF);
        const int bb = static_cast<int>(b & 0xFF);
        return std::abs(ar - br) <= tolerance &&
               std::abs(ag - bg) <= tolerance &&
               std::abs(ab - bb) <= tolerance;
    };

    int cell_x = 0;
    int cell_z = 0;
    int cell_height = 0;
    REQUIRE(find_sloped_grass(cell_x, cell_z, cell_height));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;

    engine.camera.position = {center_x, center_y + 20.0, center_z - 12.0};
    engine.camera.set_rotation({0.0, 0.8});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> framebuffer(width * height, 0u);
    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, {center_x, center_y + 1.0, center_z}, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    const int radius = 3;
    uint32_t base_color = 0;
    bool found_base = false;
    for (int dy = -radius; dy <= radius && !found_base; ++dy)
    {
        const int y = std::clamp(py + dy, 0, static_cast<int>(height) - 1);
        const uint32_t sky = sky_row_color(static_cast<size_t>(y), height, sky_top, sky_bottom,
                                               engine.world.sky.exposure);
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int x = std::clamp(px + dx, 0, static_cast<int>(width) - 1);
            const uint32_t pixel = framebuffer[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
            if (pixel != sky)
            {
                base_color = pixel;
                found_base = true;
                break;
            }
        }
    }
    REQUIRE(found_base);

    std::vector<uint32_t> samples;
    for (int dy = -radius; dy <= radius; ++dy)
    {
        const int y = std::clamp(py + dy, 0, static_cast<int>(height) - 1);
        const uint32_t sky = sky_row_color(static_cast<size_t>(y), height, sky_top, sky_bottom,
                                               engine.world.sky.exposure);
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int x = std::clamp(px + dx, 0, static_cast<int>(width) - 1);
            const uint32_t pixel = framebuffer[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
            if (pixel == sky)
            {
                continue;
            }
            if (colors_similar(pixel, base_color, 30))
            {
                samples.push_back(luminance(pixel));
            }
        }
    }

    REQUIRE(samples.size() >= 6);
    const auto minmax = std::minmax_element(samples.begin(), samples.end());
    REQUIRE((*minmax.second - *minmax.first) >= 8);
}

TEST_CASE("sky lighting is consistent across a terrain side face")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.world.sun.intensity = 0.0;
    engine.settings.shadow_enabled = false;
    engine.settings.ambient_occlusion_enabled = false;
    engine.world.sky.sky_light_scale = 1.0;

    const uint32_t sky_top = 0xFFFFFFFF;
    const uint32_t sky_bottom = 0xFF000000;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(sky_top).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(sky_bottom).to_linear();

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    constexpr int chunk_size = 16;
    const int cell_x = 0;
    const int cell_z = 0;
    const int cell_height = heights[static_cast<size_t>(cell_z * chunk_size + cell_x)];
    REQUIRE(cell_height > 0);

    const double block_size = 2.0;
    const double half = block_size * 0.5;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + cell_x * block_size;
    const double center_z = start_z + cell_z * block_size;
    const double center_y = base_y + (cell_height - 1) * block_size;
    const double face_x = center_x - half;

    constexpr double half_pi = 1.5707963267948966;
    engine.camera.position = {face_x - 8.0, center_y, center_z};
    engine.camera.set_rotation({half_pi, 0.0});

    const size_t width = 200;
    const size_t height = 160;
    std::vector<uint32_t> framebuffer(width * height, 0u);
    engine.update(framebuffer.data(), width, height);
    engine.settings.paused = false;

    const double inset = half * 0.7;
    const Vec2 top_proj = project_point(engine.camera, {face_x, center_y + inset, center_z}, width, height);
    const Vec2 bottom_proj = project_point(engine.camera, {face_x, center_y - inset, center_z}, width, height);

    REQUIRE(is_finite(top_proj.x));
    REQUIRE(is_finite(top_proj.y));
    REQUIRE(is_finite(bottom_proj.x));
    REQUIRE(is_finite(bottom_proj.y));

    const int top_x = std::clamp(static_cast<int>(std::lround(top_proj.x)), 0, static_cast<int>(width) - 1);
    const int top_y = std::clamp(static_cast<int>(std::lround(top_proj.y)), 0, static_cast<int>(height) - 1);
    const int bottom_x = std::clamp(static_cast<int>(std::lround(bottom_proj.x)), 0, static_cast<int>(width) - 1);
    const int bottom_y = std::clamp(static_cast<int>(std::lround(bottom_proj.y)), 0, static_cast<int>(height) - 1);

    double top_lum = 0.0;
    double bottom_lum = 0.0;
    REQUIRE(average_luminance(framebuffer, width, height, top_x, top_y, sky_top, sky_bottom,
                                     engine.world.sky.exposure, top_lum));
    REQUIRE(average_luminance(framebuffer, width, height, bottom_x, bottom_y, sky_top, sky_bottom,
                                     engine.world.sky.exposure, bottom_lum));
    REQUIRE(std::abs(top_lum - bottom_lum) <= 6.0);
}

TEST_CASE("normalized Blinn-Phong specular uses Schlick Fresnel")
{
    const double shininess = 24.0;
    const double ndoth = 0.8;
    const double ndotl = 0.75;
    const double f0 = 0.2;

    const double spec_normal = LightingEngine::specular_term(ndoth, 1.0, ndotl, shininess, f0);
    const double expected = ((shininess + 8.0) / (8.0 * kPi)) * std::pow(ndoth, shininess) * f0 * ndotl;
    REQUIRE(spec_normal == Catch::Approx(expected).margin(1e-6));

    const double spec_grazing = LightingEngine::specular_term(ndoth, 0.2, ndotl, shininess, f0);
    REQUIRE(spec_grazing > spec_normal);

    const double spec_low_ndotl = LightingEngine::specular_term(ndoth, 1.0, 0.2, shininess, f0);
    REQUIRE(spec_low_ndotl < spec_normal);

    REQUIRE(LightingEngine::specular_term(ndoth, 0.2, ndotl, shininess, 0.0) ==
            Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("sky visibility darkens terrain when enabled")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = false;
    engine.world.sun.intensity = 0.0;
    engine.world.sky.sky_light_scale = 1.0;
    engine.world.sky.day_zenith = ColorSrgb::from_hex(0xFFBBD7FF).to_linear();
    engine.world.sky.day_horizon = ColorSrgb::from_hex(0xFF1A2430).to_linear();

    const size_t width = 160;
    const size_t height = 120;
    std::vector<uint32_t> no_ao(width * height, 0u);
    std::vector<uint32_t> with_ao(width * height, 0u);

    SideFaceAoProbe probe{};
    REQUIRE(find_right_face_occluder(probe));

    const int chunk_size = 16;
    const double block_size = 2.0;
    const double half = block_size * 0.5;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double center_x = start_x + probe.x * block_size;
    const double center_z = start_z + probe.z * block_size;
    const double center_y = base_y + (probe.height - 1) * block_size;
    const Vec3 sample_point{center_x + half, center_y, center_z};

    const Vec3 eye{sample_point.x + 8.0, sample_point.y + 6.0, sample_point.z - 8.0};
    const Vec3 to_target{sample_point.x - eye.x, sample_point.y - eye.y, sample_point.z - eye.z};
    const double yaw = std::atan2(to_target.x, to_target.z);
    const double dist_xz = std::sqrt(to_target.x * to_target.x + to_target.z * to_target.z);
    const double pitch = std::atan2(-to_target.y, dist_xz);
    engine.camera.position = eye;
    engine.camera.set_rotation({yaw, pitch});

    engine.settings.ambient_occlusion_enabled = false;
    engine.update(no_ao.data(), width, height);
    engine.settings.ambient_occlusion_enabled = true;
    engine.update(with_ao.data(), width, height);
    engine.settings.paused = false;

    const Vec2 projected = project_point(engine.camera, sample_point, width, height);
    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);

    double avg_no = 0.0;
    double avg_with = 0.0;
    REQUIRE(average_luminance(no_ao, width, height, px, py,
                                     0xFFBBD7FF, 0xFF1A2430,
                                     engine.world.sky.exposure, avg_no));
    REQUIRE(average_luminance(with_ao, width, height, px, py,
                                     0xFFBBD7FF, 0xFF1A2430,
                                     engine.world.sky.exposure, avg_with));
    REQUIRE(avg_with < avg_no);
    REQUIRE(avg_no - avg_with > 0.3);
}

TEST_CASE("side face sky visibility responds to diagonal neighbor blocks")
{
    Terrain terrain;

    SideFaceAoProbe probe{};
    REQUIRE(find_right_face_occluder(probe));

    constexpr int kFaceRight = 3;
    constexpr int kCornerTopFront = 3;
    float visibility = 1.0f;
    REQUIRE(vertex_sky_visibility(terrain, probe.x, probe.height - 1, probe.z,
                                          kFaceRight, kCornerTopFront, &visibility));

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    const float expected = raycast_vertex_sky_visibility(heights, 16, probe.x, probe.height - 1, probe.z,
                                                         kFaceRight, kCornerTopFront);

    REQUIRE(visibility == Catch::Approx(expected).margin(0.02f));
}

TEST_CASE("side face sky visibility captures off-axis occluders")
{
    Terrain terrain;

    SideFaceAoProbe probe{};
    REQUIRE(find_right_face_occluder(probe));

    constexpr int kFaceRight = 3;
    constexpr int kCornerTopFront = 3;
    float visibility = 1.0f;
    REQUIRE(vertex_sky_visibility(terrain, probe.x, probe.height - 1, probe.z,
                                          kFaceRight, kCornerTopFront, &visibility));
    REQUIRE(visibility < 0.98f);
}

TEST_CASE("flat terrain top faces match sky visibility raycast")
{
    Terrain terrain;

    struct FlatCornerProbe
    {
        int x;
        int z;
        int height;
        int sx;
        int sz;
    };

    auto find_flat_shared_corner = [](FlatCornerProbe& out_probe) {
        const int chunk_size = 16;
        std::vector<int> heights;
        std::vector<uint32_t> top_colors;
        build_heightmap(heights, top_colors);

        auto index = [chunk_size](int x, int z) {
            return static_cast<size_t>(z * chunk_size + x);
        };

        constexpr int corners[4][2] = {
            {1, 1},
            {1, -1},
            {-1, 1},
            {-1, -1}
        };

        for (int z = 1; z < chunk_size - 1; ++z)
        {
            for (int x = 1; x < chunk_size - 1; ++x)
            {
                const int h = heights[index(x, z)];
                for (const auto& corner : corners)
                {
                    const int sx = corner[0];
                    const int sz = corner[1];
                    const int nx = x + sx;
                    const int nz = z + sz;
                    if (nx < 0 || nx >= chunk_size || nz < 0 || nz >= chunk_size)
                    {
                        continue;
                    }
                    if (heights[index(nx, z)] != h)
                    {
                        continue;
                    }
                    if (heights[index(x, nz)] > h)
                    {
                        continue;
                    }
                    if (heights[index(nx, nz)] > h)
                    {
                        continue;
                    }
                    out_probe = {x, z, h, sx, sz};
                    return true;
                }
            }
        }

        return false;
    };

    auto top_face_corner_from_offsets = [](int sx, int sz) {
        if (sx > 0 && sz > 0) return 2;
        if (sx > 0 && sz < 0) return 3;
        if (sx < 0 && sz > 0) return 1;
        return 0;
    };

    FlatCornerProbe probe{};
    REQUIRE(find_flat_shared_corner(probe));

    const int y = probe.height - 1;
    float visibility = 1.0f;
    const int corner = top_face_corner_from_offsets(probe.sx, probe.sz);
    REQUIRE(vertex_sky_visibility(terrain, probe.x, y, probe.z, kFaceTop, corner, &visibility));

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    const float expected = raycast_vertex_sky_visibility(heights, 16, probe.x, y, probe.z,
                                                         kFaceTop, corner);

    REQUIRE(visibility == Catch::Approx(expected).margin(0.02f));
}

TEST_CASE("vertex sky visibility varies within some top faces")
{
    Terrain terrain;

    constexpr int chunk_size = 16;
    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);

    bool found = false;
    float visibility = 1.0f;
    for (int z = 0; z < chunk_size && !found; ++z)
    {
        for (int x = 0; x < chunk_size && !found; ++x)
        {
            const int height = heights[static_cast<size_t>(z * chunk_size + x)];
            const int y = height - 1;
            if (y < 0)
            {
                continue;
            }
            float min_v = 1.0f;
            float max_v = 0.0f;
            for (int corner = 0; corner < 4; ++corner)
            {
                if (!vertex_sky_visibility(terrain, x, y, z, kFaceTop, corner, &visibility))
                {
                    min_v = 1.0f;
                    max_v = 0.0f;
                    break;
                }
                min_v = std::min(min_v, visibility);
                max_v = std::max(max_v, visibility);
            }
            if (max_v - min_v > 0.02f)
            {
                found = true;
            }
        }
    }

    REQUIRE(found);
}

TEST_CASE("top face sky visibility varies beyond four discrete levels")
{
    Terrain terrain;

    constexpr int chunk_size = 16;
    std::unordered_set<int> buckets;
    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    float visibility = 1.0f;

    for (int z = 0; z < chunk_size; ++z)
    {
        for (int x = 0; x < chunk_size; ++x)
        {
            const int height = heights[static_cast<size_t>(z * chunk_size + x)];
            const int y = height - 1;
            for (int corner = 0; corner < 4; ++corner)
            {
                if (!vertex_sky_visibility(terrain, x, y, z, kFaceTop, corner, &visibility))
                {
                    continue;
                }
                const int bucket = static_cast<int>(std::lround(visibility * 1000.0f));
                buckets.insert(bucket);
            }
        }
    }

    REQUIRE(buckets.size() > 4);
}

TEST_CASE("side face sky visibility varies beyond four discrete levels")
{
    Terrain terrain;

    constexpr int chunk_size = 16;
    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    const int max_height = *std::max_element(heights.begin(), heights.end());

    std::unordered_set<int> buckets;
    float visibility = 1.0f;
    constexpr int kFaceRight = 3;
    constexpr int kFaceFront = 5;

    for (int z = 0; z < chunk_size; ++z)
    {
        for (int x = 0; x < chunk_size; ++x)
        {
            for (int y = 0; y < max_height; ++y)
            {
                if (!vertex_sky_visibility(terrain, x, y, z, kFaceRight, 0, &visibility))
                {
                    break;
                }
                const int bucket_right0 = static_cast<int>(std::lround(visibility * 1000.0f));
                buckets.insert(bucket_right0);
                for (int corner = 1; corner < 4; ++corner)
                {
                    if (vertex_sky_visibility(terrain, x, y, z, kFaceRight, corner, &visibility))
                    {
                        const int bucket_right = static_cast<int>(std::lround(visibility * 1000.0f));
                        buckets.insert(bucket_right);
                    }
                }
                if (!vertex_sky_visibility(terrain, x, y, z, kFaceFront, 0, &visibility))
                {
                    break;
                }
                const int bucket_front0 = static_cast<int>(std::lround(visibility * 1000.0f));
                buckets.insert(bucket_front0);
                for (int corner = 1; corner < 4; ++corner)
                {
                    if (vertex_sky_visibility(terrain, x, y, z, kFaceFront, corner, &visibility))
                    {
                        const int bucket_front = static_cast<int>(std::lround(visibility * 1000.0f));
                        buckets.insert(bucket_front);
                    }
                }
            }
        }
    }

    REQUIRE(buckets.size() > 4);
}

TEST_CASE("camera state setters and getters")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.position = {1.0, -2.0, 3.5};
    const Vec3 pos = engine.camera.position;
    REQUIRE(pos.x == Catch::Approx(1.0));
    REQUIRE(pos.y == Catch::Approx(-2.0));
    REQUIRE(pos.z == Catch::Approx(3.5));

    engine.camera.set_rotation({0.25, -0.5});
    const Vec2 rot = engine.camera.rotation;
    REQUIRE(rot.x == Catch::Approx(0.25));
    REQUIRE(rot.y == Catch::Approx(-0.5));
}

TEST_CASE("camera movement in world and local space")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.position = {0.0, -20.0, -20.0};
    engine.camera.set_rotation({0.0, 0.0});
    engine.camera.position = engine.camera.position + Vec3{1.0, -2.0, 3.0};
    Vec3 pos = engine.camera.position;
    REQUIRE(pos.x == Catch::Approx(1.0));
    REQUIRE(pos.y == Catch::Approx(-22.0));
    REQUIRE(pos.z == Catch::Approx(-17.0));

    engine.camera.move_local({0.0, 0.0, 1.0});
    pos = engine.camera.position;
    REQUIRE(pos.x == Catch::Approx(1.0));
    REQUIRE(pos.y == Catch::Approx(-22.0));
    REQUIRE(pos.z == Catch::Approx(-16.0));

    constexpr double half_pi = 1.5707963267948966;
    engine.camera.position = {0.0, -20.0, -20.0};
    engine.camera.set_rotation({half_pi, 0.0});
    engine.camera.move_local({0.0, 0.0, 1.0});
    pos = engine.camera.position;
    REQUIRE(pos.x == Catch::Approx(1.0));
    REQUIRE(pos.z == Catch::Approx(-20.0).margin(1e-6));
}

TEST_CASE("reprojection matrices stay stable without camera motion")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.taa.enabled = true;
    const size_t width = 96;
    const size_t height = 72;
    std::vector<uint32_t> framebuffer(width * height);

    auto mat4_multiply = [](const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                {
                    sum += a.m[i][k] * b.m[k][j];
                }
                r.m[i][j] = sum;
            }
        }
        return r;
    };

    auto mat4_is_identity = [](const Mat4& m, const double eps) {
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                const double expected = (i == j) ? 1.0 : 0.0;
                if (std::fabs(m.m[i][j] - expected) > eps)
                {
                    return false;
                }
            }
        }
        return true;
    };

    engine.update(framebuffer.data(), width, height);
    engine.update(framebuffer.data(), width, height);

    const Mat4 prev = engine.post.previousVP;
    const Mat4 curr = engine.post.currentVP;
    const Mat4 inv = engine.post.inverseCurrentVP;
    const Mat4 product = mat4_multiply(curr, inv);

    INFO("PreviousVP:\n" << mat4_str(prev));
    INFO("CurrentVP:\n" << mat4_str(curr));
    INFO("CurrentVP * Inverse(CurrentVP):\n" << mat4_str(product));

    REQUIRE(mat4_near(prev, curr, 1e-12));
    REQUIRE(mat4_is_identity(product, 1e-7));
}

TEST_CASE("reprojection matrices change when camera moves")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.taa.enabled = true;
    const size_t width = 96;
    const size_t height = 72;
    std::vector<uint32_t> framebuffer(width * height);

    engine.update(framebuffer.data(), width, height);
    Vec3 pos = engine.camera.position;
    engine.camera.position = {pos.x + 1.0, pos.y, pos.z};
    engine.update(framebuffer.data(), width, height);

    const Mat4 prev = engine.post.previousVP;
    const Mat4 curr = engine.post.currentVP;
    INFO("PreviousVP:\n" << mat4_str(prev));
    INFO("CurrentVP:\n" << mat4_str(curr));
    REQUIRE_FALSE(mat4_near(prev, curr, 1e-9));
}

TEST_CASE("camera movement ignores terrain collisions")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    const int cell_x = 0;
    const int cell_z = 0;
    const int cell_height = heights[static_cast<size_t>(cell_z * 16 + cell_x)];
    REQUIRE(cell_height > 0);

    const double block_size = 2.0;
    const double half = block_size * 0.5;
    const double start_x = -(16 - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    const double block_x = start_x + cell_x * block_size;
    const double block_z = start_z + cell_z * block_size;
    const double block_y = base_y + (cell_height - 1) * block_size;

    const Vec3 start{block_x - half - 0.2, block_y, block_z};
    engine.camera.position = start;
    engine.camera.position = engine.camera.position + Vec3{0.3, 0.0, 0.0};
    Vec3 pos = engine.camera.position;

    const bool inside = std::abs(pos.x - block_x) < half &&
                        std::abs(pos.y - block_y) < half &&
                        std::abs(pos.z - block_z) < half;
    REQUIRE(inside);

    engine.camera.position = start;
    engine.camera.position = engine.camera.position + Vec3{-0.3, 0.0, 0.0};
    pos = engine.camera.position;
    REQUIRE(pos.x == Catch::Approx(start.x - 0.3));
    engine.settings.paused = false;
}

TEST_CASE("camera forward movement follows pitch")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.set_rotation({0.0, -1.2});

    const Vec3 start = engine.camera.position;
    engine.camera.move_local({0.0, 0.0, 1.0});
    const Vec3 pos = engine.camera.position;
    const Vec3 expected = rotate_yaw_pitch({0.0, 0.0, 1.0}, 0.0, -1.2) + start;
    REQUIRE(pos.x == Catch::Approx(expected.x));
    REQUIRE(pos.y == Catch::Approx(expected.y));
    REQUIRE(pos.z == Catch::Approx(expected.z));
}

TEST_CASE("RenderEngine unproject_point reconstructs world position")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 160;
    const size_t height = 120;
    const Vec3 camera_pos{2.0, -1.5, -3.0};
    const Vec2 camera_rot{0.35, -0.2};
    engine.camera.position = camera_pos;
    engine.camera.set_rotation(camera_rot);

    const Vec3 target{10.0, 0.0, 5.0};
    const Vec2 projected = project_point(engine.camera, target, width, height);
    REQUIRE_FALSE(is_nan(projected.x));
    REQUIRE_FALSE(is_nan(projected.y));

    const Vec3 view = rotate_yaw_pitch({target.x - camera_pos.x,
                                        target.y - camera_pos.y,
                                        target.z - camera_pos.z},
                                       -camera_rot.x, -camera_rot.y);
    REQUIRE(view.z > 0.0);

    const Vec3 reconstructed = unproject_point(engine.camera, {projected.x, projected.y, view.z},
                                                      width, height);
    REQUIRE(reconstructed.x == Catch::Approx(target.x).margin(1e-6));
    REQUIRE(reconstructed.y == Catch::Approx(target.y).margin(1e-6));
    REQUIRE(reconstructed.z == Catch::Approx(target.z).margin(1e-6));
}

TEST_CASE("RenderEngine reproject_point maps world position into previous frame")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.set_rotation({0.0, 0.0});
    const size_t width = 160;
    const size_t height = 120;
    std::vector<uint32_t> framebuffer(width * height);

    engine.camera.position = {0.0, 0.0, 0.0};
    engine.update(framebuffer.data(), width, height);

    engine.camera.position = {-10.0, 0.0, 0.0};
    engine.update(framebuffer.data(), width, height);

    const Vec3 point{0.0, 0.0, 100.0};
    const Vec2 current = project_point(engine.camera, point, width, height);
    REQUIRE(current.x > width / 2.0);

    const Vec3 camera_pos = engine.camera.position;
    const double view_z = point.z - camera_pos.z;
    REQUIRE(view_z > 0.0);

    const Vec3 reconstructed = unproject_point(engine.camera, {current.x, current.y, view_z}, width, height);
    REQUIRE(reconstructed.x == Catch::Approx(point.x).margin(1e-6));
    REQUIRE(reconstructed.y == Catch::Approx(point.y).margin(1e-6));
    REQUIRE(reconstructed.z == Catch::Approx(point.z).margin(1e-6));

    const Vec2 prev = reproject_point(engine.post.previousVP, reconstructed, width, height);
    REQUIRE(prev.x == Catch::Approx(width / 2.0).margin(1e-6));
    REQUIRE(prev.y == Catch::Approx(height / 2.0).margin(1e-6));

    const double fov_x = static_cast<double>(height) * 0.8;
    const double expected_delta = (point.x - camera_pos.x) / view_z * fov_x;
    REQUIRE((current.x - prev.x) == Catch::Approx(expected_delta).margin(1e-6));
}

TEST_CASE("reprojection alignment maps scene points back to previous screen coordinates")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.taa.enabled = true;
    engine.settings.taa.clamp_enabled = false;
    engine.camera.position = {0.0, 0.0, -4.0};
    engine.camera.set_rotation({0.0, 0.0});

    const size_t width = 160;
    const size_t height = 120;
    std::vector<uint32_t> framebuffer(width * height, 0u);

    engine.update(framebuffer.data(), width, height);

    const Vec3 point{0.0, 0.0, 12.0};
    const Vec2 p1 = project_point(engine.camera, point, width, height);
    REQUIRE(is_finite(p1.x));
    REQUIRE(is_finite(p1.y));

    Vec3 cam_pos = engine.camera.position;
    engine.camera.position = {cam_pos.x + 2.0, cam_pos.y, cam_pos.z};

    engine.update(framebuffer.data(), width, height);

    const Vec2 p2 = project_point(engine.camera, point, width, height);
    REQUIRE(is_finite(p2.x));
    REQUIRE(is_finite(p2.y));

    cam_pos = engine.camera.position;
    const Vec2 cam_rot = engine.camera.rotation;
    const Vec3 view = rotate_yaw_pitch({point.x - cam_pos.x,
                                        point.y - cam_pos.y,
                                        point.z - cam_pos.z},
                                       -cam_rot.x, -cam_rot.y);
    REQUIRE(view.z > 0.0);

    const Vec3 reconstructed = unproject_point(engine.camera, {p2.x, p2.y, view.z}, width, height);
    const Vec2 prev = reproject_point(engine.post.previousVP, reconstructed, width, height);

    REQUIRE(prev.x == Catch::Approx(p1.x).margin(1e-4));
    REQUIRE(prev.y == Catch::Approx(p1.y).margin(1e-4));

    engine.settings.taa.clamp_enabled = true;
}

TEST_CASE("temporal accumulation rejects history for pixels reprojected outside the frame")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.settings.paused = true;
    engine.settings.taa.enabled = true;
    engine.settings.taa.blend = 0.1;
    engine.settings.taa.clamp_enabled = false;

    const size_t width = 160;
    const size_t height = 120;
    const uint32_t sky_top = pack_color(engine.world.sky.day_zenith.to_srgb());
    const uint32_t sky_bottom = pack_color(engine.world.sky.day_horizon.to_srgb());

    std::vector<uint32_t> frame1(width * height, 0u);
    std::vector<uint32_t> frame_with_history(width * height, 0u);
    std::vector<uint32_t> frame_current(width * height, 0u);

    std::vector<int> heights;
    std::vector<uint32_t> top_colors;
    build_heightmap(heights, top_colors);
    const int chunk_size = 16;
    const double block_size = 2.0;
    const double half = block_size * 0.5;
    const double start_x = -(chunk_size - 1) * block_size * 0.5;
    const double start_z = 4.0;
    const double base_y = 2.0;

    auto reproject_with_vp = [](const Mat4& vp, const Vec3 world, size_t width, size_t height,
                                double near_plane) {
        if (width == 0 || height == 0)
        {
            return Vec2{0.0, 0.0};
        }

        const double clip_x = vp.m[0][0] * world.x + vp.m[0][1] * world.y + vp.m[0][2] * world.z + vp.m[0][3];
        const double clip_y = vp.m[1][0] * world.x + vp.m[1][1] * world.y + vp.m[1][2] * world.z + vp.m[1][3];
        const double clip_w = vp.m[3][0] * world.x + vp.m[3][1] * world.y + vp.m[3][2] * world.z + vp.m[3][3];
        if (clip_w <= near_plane)
        {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            return Vec2{nan, nan};
        }

        const double inv_w = 1.0 / clip_w;
        const double ndc_x = clip_x * inv_w;
        const double ndc_y = clip_y * inv_w;
        const double screen_x = (ndc_x * 0.5 + 0.5) * static_cast<double>(width);
        const double screen_y = (ndc_y * 0.5 + 0.5) * static_cast<double>(height);
        return Vec2{screen_x, screen_y};
    };

    bool found = false;
    size_t target_idx = 0;
    Vec2 prev_screen{};
    constexpr double yaw_delta = 2.2;

    struct CameraConfig
    {
        Vec3 pos;
        Vec2 rot;
    };

    const std::array<CameraConfig, 3> configs = {{
        {{0.0, 8.0, -4.0}, {0.0, 0.6}},
        {{0.0, 10.0, 10.0}, {0.0, 0.7}},
        {{0.0, 12.0, 20.0}, {0.0, 0.7}}
    }};

    for (const auto& cfg : configs)
    {

        engine.camera.position = cfg.pos;
        engine.camera.set_rotation(cfg.rot);
        engine.update(frame1.data(), width, height);

        engine.camera.set_rotation({cfg.rot.x + yaw_delta, cfg.rot.y});
        engine.update(frame_with_history.data(), width, height);
        const Mat4 prev_vp = engine.post.previousVP;


        engine.update(frame_current.data(), width, height);

        for (int pass = 0; pass < 2 && !found; ++pass)
        {
            const bool require_diff = (pass == 0);
            const int left_limit = (pass == 0) ? 12 : 20;

            for (int z = 0; z < chunk_size && !found; ++z)
            {
                for (int x = 0; x < chunk_size && !found; ++x)
                {
                    const int h = heights[static_cast<size_t>(z * chunk_size + x)];
                    if (h <= 0)
                    {
                        continue;
                    }
                    const double block_x = start_x + x * block_size;
                    const double block_z = start_z + z * block_size;
                    const double block_y = base_y + (h - 1) * block_size;
                    const Vec3 world{block_x, block_y + half, block_z};

                    const Vec2 projected = project_point(engine.camera, world, width, height);
                    if (!is_finite(projected.x) || !is_finite(projected.y))
                    {
                        continue;
                    }
                    if (projected.x < 0.0 || projected.x >= static_cast<double>(width) ||
                        projected.y < 0.0 || projected.y >= static_cast<double>(height))
                    {
                        continue;
                    }
                    const int px = std::clamp(static_cast<int>(std::lround(projected.x)), 0, static_cast<int>(width) - 1);
                    const int py = std::clamp(static_cast<int>(std::lround(projected.y)), 0, static_cast<int>(height) - 1);
                    if (px > left_limit)
                    {
                        continue;
                    }

                    const uint32_t sky = sky_row_color(static_cast<size_t>(py), height, sky_top, sky_bottom,
                                                           engine.world.sky.exposure);
                    const size_t idx = static_cast<size_t>(py) * width + static_cast<size_t>(px);
                    if (frame_current[idx] == sky)
                    {
                        continue;
                    }
                    if (require_diff)
                    {
                        const int diff = std::abs(static_cast<int>(luminance(frame_current[idx])) -
                                                  static_cast<int>(luminance(frame1[idx])));
                        if (diff < 30)
                        {
                            continue;
                        }
                    }

                    const Vec2 prev = reproject_with_vp(prev_vp, world, width, height,
                                                        Camera::near_plane);
                    if (!is_finite(prev.x) || !is_finite(prev.y))
                    {
                        continue;
                    }
                    if (prev.x >= 0.0 && prev.x <= static_cast<double>(width) &&
                        prev.y >= 0.0 && prev.y <= static_cast<double>(height))
                    {
                        continue;
                    }

                    found = true;
                    target_idx = idx;
                    prev_screen = prev;
                }
            }
        }
        if (found)
        {
            break;
        }
    }

    INFO("Prev screen coord: " << prev_screen.x << ", " << prev_screen.y);
    REQUIRE(found);

    const int output_delta = std::abs(static_cast<int>(luminance(frame_with_history[target_idx])) -
                                      static_cast<int>(luminance(frame_current[target_idx])));
    INFO("Output delta: " << output_delta);
    REQUIRE(output_delta <= 2);

    engine.settings.paused = false;
    engine.settings.taa.clamp_enabled = true;
}

TEST_CASE("history bilinear sampling blends across top row midpoint")
{
    const size_t width = 2;
    const size_t height = 2;
    const std::array<Vec3, 4> history = {{
        {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0},
        {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}
    }};
    const Vec3 sample = sample_history(history.data(), width, height, {1.0, 0.5});
    REQUIRE(sample.x == Catch::Approx(0.5));
    REQUIRE(sample.y == Catch::Approx(0.5));
    REQUIRE(sample.z == Catch::Approx(0.5));
}

TEST_CASE("history bilinear sampling uses fractional weights")
{
    const size_t width = 2;
    const size_t height = 2;
    const std::array<Vec3, 4> history = {{
        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0}, {1.0, 1.0, 0.0}
    }};
    const Vec3 sample = sample_history(history.data(), width, height, {1.25, 0.75});
    REQUIRE(sample.x == Catch::Approx(0.375));
    REQUIRE(sample.y == Catch::Approx(0.75));
    REQUIRE(sample.z == Catch::Approx(0.0625));
}

TEST_CASE("RenderEngine project_point centers consistently")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 100;
    const size_t height = 80;

    const Vec2 center = project_point(engine.camera, {0.0, 0.0, 0.0}, width, height);
    REQUIRE(center.x == Catch::Approx(width / 2.0));
    REQUIRE(center.y == Catch::Approx(height / 2.0));

    const Vec2 square = project_point(engine.camera, {1.0, 0.0, 0.0}, 80, 80);
    const Vec2 wide = project_point(engine.camera, {1.0, 0.0, 0.0}, 200, 80);
    const double square_offset = square.x - 40.0;
    const double wide_offset = wide.x - 100.0;
    REQUIRE(wide_offset == Catch::Approx(square_offset));
}

TEST_CASE("RenderEngine project_point returns NaN for points behind camera")
{
    RenderEngine engine;
    reset_camera(engine);
    engine.camera.position = {0.0, 0.0, 0.0};
    engine.camera.set_rotation({0.0, 0.0});
    const size_t width = 120;
    const size_t height = 90;

    const Vec2 projected = project_point(engine.camera, {0.0, 0.0, -1.0}, width, height);
    REQUIRE(is_nan(projected.x));
    REQUIRE(is_nan(projected.y));
}

TEST_CASE("sample_depth uses perspective-correct interpolation")
{
    const Vec3 v0{10.0, 10.0, 1.0};
    const Vec3 v1{30.0, 10.0, 4.0};
    const Vec3 v2{10.0, 30.0, 8.0};
    const Vec2 p{15.0, 15.0};

    float depth = 0.0f;
    REQUIRE(sample_depth(v0, v1, v2, p, &depth));

    auto edge = [](const Vec3& a, const Vec3& b, const Vec2& c) {
        return static_cast<float>((c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x));
    };

    const float area = edge(v0, v1, {v2.x, v2.y});
    REQUIRE(area != 0.0f);
    float w0 = edge(v1, v2, p) / area;
    float w1 = edge(v2, v0, p) / area;
    float w2 = edge(v0, v1, p) / area;

    const float inv_z = w0 * (1.0f / static_cast<float>(v0.z)) +
                        w1 * (1.0f / static_cast<float>(v1.z)) +
                        w2 * (1.0f / static_cast<float>(v2.z));
    const float expected = 1.0f / inv_z;

    REQUIRE(depth == Catch::Approx(expected).margin(1e-4f));
}

TEST_CASE("RenderEngine should_rasterize allows near-plane clipping")
{
    RenderEngine engine;
    const Vec3 in_front{0.0, 0.0, 0.2};
    const Vec3 behind{0.0, 0.0, 0.01};

    REQUIRE(should_rasterize(engine.rasterizer, in_front, in_front, in_front));
    REQUIRE(should_rasterize(engine.rasterizer, behind, in_front, in_front));
    REQUIRE_FALSE(should_rasterize(engine.rasterizer, behind, behind, behind));
}

TEST_CASE("RenderEngine clip_near clips partially occluded triangles")
{
    RenderEngine engine;
    const double near_plane = Camera::near_plane;
    const Vec3 in_front{0.0, 0.0, near_plane + 0.1};
    const Vec3 behind{0.0, 0.0, near_plane * 0.5};

    Vec3 clipped[4]{};
    const size_t count = clip_near(engine.rasterizer, behind, in_front, in_front, clipped, 4);
    REQUIRE(count == 4);
    for (size_t i = 0; i < count; ++i)
    {
        REQUIRE(clipped[i].z >= Catch::Approx(near_plane).margin(1e-6));
    }

    const size_t count_inside = clip_near(engine.rasterizer, in_front, in_front, in_front, clipped, 4);
    REQUIRE(count_inside == 3);

    const size_t count_outside = clip_near(engine.rasterizer, behind, behind, behind, clipped, 4);
    REQUIRE(count_outside == 0);
}

TEST_CASE("RenderEngine project_point responds to yaw rotation")
{
    RenderEngine engine;
    reset_camera(engine);
    const size_t width = 120;
    const size_t height = 90;
    engine.camera.position = {0.0, 0.0, 0.0};

    const Vec3 point{10.0, 0.0, 10.0};

    engine.camera.set_rotation({0.0, 0.0});
    const Vec2 no_yaw = project_point(engine.camera, point, width, height);
    REQUIRE(no_yaw.x > width / 2.0);

    const double quarter_pi = 0.7853981633974483;
    engine.camera.set_rotation({quarter_pi, 0.0});
    const Vec2 rotated = project_point(engine.camera, point, width, height);
    REQUIRE(rotated.x == Catch::Approx(width / 2.0).margin(1e-6));
}
