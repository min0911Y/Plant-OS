module;

#include "../prelude.hpp"

export module terrain;

import math;
import noise;

export struct Material
{
    uint32_t color = 0xFFFFFFFF;
    double ambient = 0.25;
    double diffuse = 1.0;
    double specular = 0.15;
    double shininess = 24.0;
};

export struct BlockGeometry
{
    static constexpr int FaceTop = 0;
    static constexpr int FaceBottom = 1;
    static constexpr int FaceLeft = 2;
    static constexpr int FaceRight = 3;
    static constexpr int FaceBack = 4;
    static constexpr int FaceFront = 5;
    static constexpr int kFaceCount = 6;
    static constexpr int kCornersPerFace = 4;

    static constexpr std::array<Vec3, 8> corners = {{
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0}
    }};

    static constexpr std::array<std::array<int, 4>, 6> face_corners = {{
        {{3, 7, 6, 2}}, // top (+y)
        {{0, 1, 5, 4}}, // bottom (-y)
        {{0, 4, 7, 3}}, // left (-x)
        {{1, 2, 6, 5}}, // right (+x)
        {{0, 3, 2, 1}}, // back (-z)
        {{4, 5, 6, 7}}  // front (+z)
    }};

    static constexpr std::array<std::array<int, 3>, 6> steps = {{
        {{0, 1, 0}},   // top (+y)
        {{0, -1, 0}},  // bottom (-y)
        {{-1, 0, 0}},  // left (-x)
        {{1, 0, 0}},   // right (+x)
        {{0, 0, -1}},  // back (-z)
        {{0, 0, 1}}    // front (+z)
    }};

    [[nodiscard]]
    static constexpr auto normal(const int face) -> Vec3
    {
        return {static_cast<double>(steps[face][0]),
                static_cast<double>(steps[face][1]),
                static_cast<double>(steps[face][2])};
    }
};

export struct TerrainConfig
{
    int chunk_size = 16;
    double block_size = 2.0;
    double start_z = 4.0;
    double base_y = 2.0;
    int base_height = 4;
    int dirt_thickness = 2;
    int height_variation = 6;
    double height_freq = 0.12;
    double surface_freq = 0.4;

    std::vector<Material> palette{
        {.color = 0xFF7A7A7A, .ambient = 0.22, .diffuse = 0.90,
         .specular = 0.06, .shininess = 32.0},  // stone
        {.color = 0xFF7D4714, .ambient = 0.24, .diffuse = 0.95,
         .specular = 0.02, .shininess = 8.0},   // dirt
        {.color = 0xFF3B8A38, .ambient = 0.28, .diffuse = 1.00,
         .specular = 0.025, .shininess = 12.0}, // grass
        {.color = 0xFF2B5FA8, .ambient = 0.18, .diffuse = 0.70,
         .specular = 0.08, .shininess = 96.0},  // water
    };
    uint8_t stone = 0;
    uint8_t dirt = 1;
    uint8_t grass = 2;
    uint8_t water = 3;
};

export struct VoxelBlock
{
    Vec3 position;
    uint8_t material;
    std::array<std::array<float, 4>, 6> sky_visibility;

    [[nodiscard]]
    auto face_visibility(const int face) const -> float
    {
        const auto& corners = sky_visibility[static_cast<size_t>(face)];
        return (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
    }
};

export struct RenderQuad
{
    std::array<Vec3, 4> v;
    std::array<float, 4> sky_visibility;
    Vec3 normal;
    uint8_t material;
};

export struct RayHit
{
    Vec3 position;
    Vec3 normal;
    int face;
    const VoxelBlock* block;
    double distance;
};

export struct BlockTopology
{
    int chunk_size = 0;
    int max_height = 0;
    std::vector<int> heights;
    std::vector<int> block_index;

    [[nodiscard]]
    constexpr auto index(const int gx, const int gz) const -> size_t
    {
        return static_cast<size_t>(gz * chunk_size + gx);
    }

    [[nodiscard]]
    constexpr auto block_slot(const int gx, const int gy, const int gz) const -> size_t
    {
        const size_t width = static_cast<size_t>(chunk_size);
        const size_t height = static_cast<size_t>(max_height);
        return (static_cast<size_t>(gz) * height + static_cast<size_t>(gy)) * width
               + static_cast<size_t>(gx);
    }

    [[nodiscard]]
    auto has_block(const int gx, const int gy, const int gz) const -> bool
    {
        if (gx < 0 || gx >= chunk_size || gz < 0 || gz >= chunk_size || gy < 0)
        {
            return false;
        }
        const size_t idx = index(gx, gz);
        return idx < heights.size() && gy < heights[idx];
    }

    [[nodiscard]]
    auto block_at(std::span<const VoxelBlock> blocks,
                  const int gx, const int gy, const int gz) const -> const VoxelBlock*
    {
        if (gx < 0 || gx >= chunk_size || gz < 0 || gz >= chunk_size ||
            gy < 0 || gy >= max_height)
        {
            return nullptr;
        }
        const size_t slot = block_slot(gx, gy, gz);
        if (slot >= block_index.size())
        {
            return nullptr;
        }
        const int idx = block_index[slot];
        if (idx < 0 || static_cast<size_t>(idx) >= blocks.size())
        {
            return nullptr;
        }
        return &blocks[static_cast<size_t>(idx)];
    }
};

namespace {

struct Occlusion
{
    Occlusion() = delete;

    [[nodiscard]]
    static auto sample(const BlockTopology& topology,
                       const int gx, const int gy, const int gz,
                       const int face, const int corner) -> float
    {
        const Vec3 normal = BlockGeometry::normal(face);
        auto [tangent, bitangent, forward] = Vec3::get_basis(normal);

        const Vec3 grid_pos{
            static_cast<double>(gx),
            static_cast<double>(gy),
            static_cast<double>(gz)
        };
        const int vi = BlockGeometry::face_corners[face][corner];
        const Vec3 vertex = grid_pos + BlockGeometry::corners[vi];
        const Vec3 center = grid_pos + Vec3{0.5, 0.5, 0.5};

        const Vec3 origin = vertex +
                            forward * kRayBias +
                            (center - vertex) * kRayCenterBias;

        const auto& samples = sample_dirs();
        size_t occluded = 0;
        for (const auto& sample : samples)
        {
            const Vec3 dir = tangent * sample.x +
                             bitangent * sample.y +
                             forward * sample.z;

            bool hit = false;
            for (double t = kRayStep; t <= kRayMaxDistance; t += kRayStep)
            {
                const Vec3 p = origin + dir * t;
                const int vx = static_cast<int>(std::floor(p.x));
                const int vy = static_cast<int>(std::floor(p.y));
                const int vz = static_cast<int>(std::floor(p.z));
                if (topology.has_block(vx, vy, vz))
                {
                    hit = true;
                    break;
                }
            }
            if (hit) occluded++;
        }

        const double occlusion_ratio = static_cast<double>(occluded)
                                       / static_cast<double>(samples.size());
        return static_cast<float>(std::clamp(1.0 - occlusion_ratio, 0.0, 1.0));
    }

private:
    static constexpr size_t kRayCount = 128;
    static constexpr double kRayStep = 0.25;
    static constexpr double kRayMaxDistance = 6.0;
    static constexpr double kRayBias = 0.02;
    static constexpr double kRayCenterBias = 0.02;

    [[nodiscard]]
    static auto sample_dirs() -> const std::array<Vec3, kRayCount>&
    {
        static const auto dirs = [] {
            std::array<Vec3, kRayCount> samples{};
            constexpr double total_rays = static_cast<double>(kRayCount);

            for (size_t i = 0; i < kRayCount; ++i)
            {
                const double u = (static_cast<double>(i) + 0.5) / total_rays;
                const double v = radical_inverse_vdc(static_cast<uint32_t>(i));

                const double r = std::sqrt(u);
                const double theta = 2.0 * std::numbers::pi_v<double> * v;
                samples[i] = {r * std::cos(theta), r * std::sin(theta),
                              std::sqrt(std::max(0.0, 1.0 - u))};
            }
            return samples;
        }();
        return dirs;
    }

    static auto radical_inverse_vdc(uint32_t bits) -> double
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<double>(bits) * 2.3283064365386963e-10;
    }
};

}

export struct Terrain
{
    TerrainConfig config{};
    BlockTopology topology;
    std::vector<VoxelBlock> blocks;
    std::vector<RenderQuad> mesh;
    std::vector<LinearColor> albedo;
    size_t visible_faces = 0;

    auto generate() -> void
    {
        albedo.clear();
        albedo.reserve(config.palette.size());
        for (const Material& material : config.palette)
        {
            albedo.push_back(ColorSrgb::from_hex(material.color).to_linear());
        }
        build_chunk();
        build_mesh();
    }

    [[nodiscard]]
    auto grid_origin() const -> Vec3
    {
        const double half = config.block_size * 0.5;
        const double start_x = -(static_cast<double>(topology.chunk_size) - 1.0) * half;
        return {start_x - half, config.base_y - half, config.start_z - half};
    }

    [[nodiscard]]
    auto raycast(const Vec3& origin, const Vec3& dir,
                 const double max_distance = std::numeric_limits<double>::infinity()) const
        -> std::optional<RayHit>
    {
        const int size = topology.chunk_size;
        const int max_height = topology.max_height;
        if (size <= 0 || max_height <= 0 ||
            (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0))
        {
            return std::nullopt;
        }

        constexpr double inf = std::numeric_limits<double>::infinity();
        const Vec3 grid = (origin - grid_origin()) * (1.0 / config.block_size);
        const std::array<double, 3> g{grid.x, grid.y, grid.z};
        const std::array<double, 3> d{dir.x, dir.y, dir.z};

        std::array<int, 3> cell{};
        std::array<int, 3> step{};
        std::array<double, 3> t_delta{};
        std::array<double, 3> t_max{};
        for (int i = 0; i < 3; ++i)
        {
            cell[i] = static_cast<int>(std::floor(g[i]));
            step[i] = (d[i] > 0.0) - (d[i] < 0.0);
            t_delta[i] = step[i] != 0 ? config.block_size / std::abs(d[i]) : inf;
            const double boundary = std::floor(g[i]) + (step[i] > 0 ? 1.0 : 0.0);
            t_max[i] = step[i] != 0 ? (boundary - g[i]) * config.block_size / d[i] : inf;
        }

        const auto in_bounds = [&] {
            return cell[0] >= 0 && cell[0] < size &&
                   cell[1] >= 0 && cell[1] < max_height &&
                   cell[2] >= 0 && cell[2] < size;
        };
        if (!in_bounds())
        {
            return std::nullopt;
        }

        static constexpr std::array<std::array<int, 2>, 3> kEnteredFace{{
            {BlockGeometry::FaceRight, BlockGeometry::FaceLeft},
            {BlockGeometry::FaceTop, BlockGeometry::FaceBottom},
            {BlockGeometry::FaceFront, BlockGeometry::FaceBack}
        }};

        while (true)
        {
            const int axis = t_max[0] < t_max[1]
                                 ? (t_max[0] < t_max[2] ? 0 : 2)
                                 : (t_max[1] < t_max[2] ? 1 : 2);
            const double t = t_max[axis];
            if (t > max_distance)
            {
                return std::nullopt;
            }
            t_max[axis] += t_delta[axis];
            cell[axis] += step[axis];
            if (!in_bounds())
            {
                return std::nullopt;
            }
            if (!topology.has_block(cell[0], cell[1], cell[2]))
            {
                continue;
            }

            const int face = kEnteredFace[axis][step[axis] > 0 ? 1 : 0];
            const VoxelBlock* block = topology.block_at(blocks, cell[0], cell[1], cell[2]);
            if (!block)
            {
                return std::nullopt;
            }
            return RayHit{origin + dir * t, BlockGeometry::normal(face), face, block, t};
        }
    }

private:
    auto build_chunk() -> void
    {
        const int chunk_size = config.chunk_size;
        topology.chunk_size = chunk_size;

        const size_t grid_cells = static_cast<size_t>(chunk_size) * chunk_size;
        topology.heights.assign(grid_cells, 0);
        std::vector<uint8_t> surface(grid_cells, config.grass);
        blocks.clear();
        blocks.reserve(grid_cells * static_cast<size_t>(config.base_height
                                                        + config.height_variation + 3));

        topology.max_height = 0;
        for (int z = 0; z < chunk_size; ++z)
        {
            for (int x = 0; x < chunk_size; ++x)
            {
                const size_t idx = topology.index(x, z);
                topology.heights[idx] = height_at(x, z);
                surface[idx] = surface_material_at(x, z);
                topology.max_height = std::max(topology.max_height, topology.heights[idx]);
            }
        }

        const size_t slots = grid_cells * static_cast<size_t>(std::max(topology.max_height, 1));
        topology.block_index.assign(slots, -1);

        const Vec3 base = grid_origin();
        for (size_t i = 0; i < grid_cells; ++i)
        {
            const int height = topology.heights[i];
            const int z = static_cast<int>(i) / chunk_size;
            const int x = static_cast<int>(i) % chunk_size;

            for (int y = 0; y < height; ++y)
            {
                const Vec3 center{0.5 + x, 0.5 + y, 0.5 + z};
                blocks.push_back({
                    base + center * config.block_size,
                    material_at(y, height, surface[i]),
                    face_sky(x, y, z)
                });
                topology.block_index[topology.block_slot(x, y, z)] =
                    static_cast<int>(blocks.size() - 1);
            }
        }
    }

    auto build_mesh() -> void
    {
        mesh.clear();
        visible_faces = 0;

        for (const VoxelBlock& block : blocks)
        {
            const Vec3 grid = (block.position - grid_origin()) * (1.0 / config.block_size);
            const int x = static_cast<int>(std::floor(grid.x));
            const int y = static_cast<int>(std::floor(grid.y));
            const int z = static_cast<int>(std::floor(grid.z));

            for (int face = 0; face < BlockGeometry::kFaceCount; ++face)
            {
                const auto& step = BlockGeometry::steps[face];
                if (topology.has_block(x + step[0], y + step[1], z + step[2]))
                {
                    continue;
                }

                RenderQuad quad{};
                quad.material = block.material;
                quad.normal = BlockGeometry::normal(face);
                quad.sky_visibility = block.sky_visibility[static_cast<size_t>(face)];
                for (int corner = 0; corner < BlockGeometry::kCornersPerFace; ++corner)
                {
                    const int vi = BlockGeometry::face_corners[face][corner];
                    const Vec3 offset = BlockGeometry::corners[vi] - Vec3{0.5, 0.5, 0.5};
                    quad.v[corner] = block.position + offset * config.block_size;
                }
                mesh.push_back(quad);
                visible_faces++;
            }
        }
    }

    [[nodiscard]]
    auto height_at(const int x, const int z) const -> int
    {
        const double h = SimplexNoise::sample(x * config.height_freq, z * config.height_freq);
        const double scaled = (h + 1.0) * 0.5 * static_cast<double>(config.height_variation);
        return std::max(config.base_height + static_cast<int>(scaled + 0.5), 3);
    }

    [[nodiscard]]
    auto surface_material_at(const int x, const int z) const -> uint8_t
    {
        const double surface = SimplexNoise::sample(x * config.surface_freq + 100.0,
                                                    z * config.surface_freq - 100.0);
        if (surface > 0.55)
        {
            return config.water;
        }
        if (surface < -0.35)
        {
            return config.dirt;
        }
        return config.grass;
    }

    [[nodiscard]]
    auto material_at(const int y, const int height, const uint8_t surface) const -> uint8_t
    {
        if (y >= height - 1)
        {
            return surface;
        }
        if (y >= height - 1 - config.dirt_thickness)
        {
            return config.dirt;
        }
        return config.stone;
    }

    [[nodiscard]]
    auto face_sky(const int x, const int y, const int z) const
        -> std::array<std::array<float, 4>, 6>
    {
        std::array<std::array<float, 4>, 6> sky{};
        for (int face = 0; face < BlockGeometry::kFaceCount; ++face)
        {
            const auto& step = BlockGeometry::steps[face];
            if (topology.has_block(x + step[0], y + step[1], z + step[2]))
            {
                continue;
            }
            for (int corner = 0; corner < BlockGeometry::kCornersPerFace; ++corner)
            {
                sky[face][corner] = Occlusion::sample(topology, x, y, z, face, corner);
            }
        }
        return sky;
    }
};
