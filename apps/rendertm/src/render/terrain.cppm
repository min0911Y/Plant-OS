module;

#include "../prelude.hpp"

export module terrain;

import math;

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

export struct TerrainConfig {
  // One palette index per voxel, shared by all room furnishings.
  enum Surface : uint8_t {
    Plaster, Limestone, Grout, Coral, Teal, Oak, Gold, Indigo,
    Rose, Leaf, Mint, Porcelain, Soil
  };

  int chunk_size = 32;
  double block_size = 1.0;
  double start_z = 4.0;
  double base_y = 2.0;

  std::vector<Material> palette{
      {.color = 0xFFF0E9D8}, // warm plaster
      {.color = 0xFFD6CDB8}, // limestone
      {.color = 0xFF797C80}, // tile joints
      {.color = 0xFFE65343}, // coral upholstery / west wall
      {.color = 0xFF25B8AE}, // teal upholstery / east wall
      {.color = 0xFFAE7547}, // oak
      {.color = 0xFFF2BE42}, // ochre ceramic
      {.color = 0xFF535AB8}, // indigo
      {.color = 0xFFDC76AA}, // rose
      {.color = 0xFF43854B}, // foliage
      {.color = 0xFF91C966}, // new leaves
      {.color = 0xFFFAF5E9, .specular = 0.3, .shininess = 64.0},
      {.color = 0xFF514237}, // potting soil
  };
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
    std::vector<int> block_index;

    [[nodiscard]]
    constexpr auto block_slot(const int gx, const int gy, const int gz) const -> size_t
    {
        const size_t width = static_cast<size_t>(chunk_size);
        const size_t height = static_cast<size_t>(max_height);
        return (static_cast<size_t>(gz) * height + static_cast<size_t>(gy)) * width
               + static_cast<size_t>(gx);
    }

  [[nodiscard]]
  auto has_block(const int gx, const int gy, const int gz) const -> bool {
    if (gx < 0 || gx >= chunk_size || gz < 0 || gz >= chunk_size ||
        gy < 0 || gy >= max_height) {
      return false;
    }
    const size_t slot = block_slot(gx, gy, gz);
    return slot < block_index.size() && block_index[slot] >= 0;
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
  auto build_chunk() -> void {
    using enum TerrainConfig::Surface;
    // Design coordinates describe a 32 x 16 x 32 voxel room. Scaling the
    // horizontal grid preserves the layout when chunk_size changes.
    constexpr int design_size = 32;
    constexpr int room_height = 16;
    const int size = std::max(config.chunk_size, 1);
    topology.chunk_size = size;
    topology.max_height = room_height;
    const size_t slots = static_cast<size_t>(size) * size * room_height;
    topology.block_index.assign(slots, -1);
    blocks.clear();
    const Vec3 base = grid_origin();

    // Half-open boxes overwrite materials without duplicating occupied cells.
    const auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1,
                         TerrainConfig::Surface material) {
      x0 = static_cast<int>(static_cast<int64_t>(x0) * size / design_size);
      x1 = static_cast<int>(static_cast<int64_t>(x1) * size / design_size);
      z0 = static_cast<int>(static_cast<int64_t>(z0) * size / design_size);
      z1 = static_cast<int>(static_cast<int64_t>(z1) * size / design_size);
      for (int z = z0; z < z1; ++z) {
        for (int y = y0; y < y1; ++y) {
          for (int x = x0; x < x1; ++x) {
            int &index = topology.block_index[topology.block_slot(x, y, z)];
            if (index >= 0) {
              blocks[static_cast<size_t>(index)].material = material;
              continue;
            }
            index = static_cast<int>(blocks.size());
            blocks.push_back({base + Vec3{0.5 + x, 0.5 + y, 0.5 + z} *
                                        config.block_size,
                              static_cast<uint8_t>(material), {}});
          }
        }
      }
    };

    // Pale surfaces receive colored bounce light; the +/-Z ends stay open.
    box(0, 0, 0, 32, 1, 32, Grout);
    for (int z = 0; z < 32; z += 4) {
      for (int x = 0; x < 32; x += 4) {
        box(x, 0, z, x + 3, 1, z + 3, Limestone);
      }
    }
    box(0, 1, 0, 1, 16, 32, Plaster);
    box(31, 1, 0, 32, 16, 32, Plaster);
    box(1, 2, 3, 2, 10, 29, Coral);
    box(30, 2, 3, 31, 10, 29, Teal);
    for (int z = 2; z < 32; z += 9) {
      box(1, 1, z, 2, 15, z + 1, Plaster);
      box(30, 1, z, 31, 15, z + 1, Plaster);
    }
    // A broad front skylight and a covered rear gallery give both sunlit
    // patches and deep shade without changing the existing sky or lights.
    box(0, 15, 0, 5, 16, 32, Plaster);
    box(28, 15, 0, 32, 16, 32, Plaster);
    box(5, 15, 22, 28, 16, 32, Plaster);
    box(5, 15, 0, 28, 16, 1, Oak);
    box(5, 15, 13, 28, 16, 14, Oak);

    // West lounge: indigo rug, coral sofa, contrasting loose cushions.
    box(3, 1, 5, 13, 2, 16, Indigo);
    box(4, 1, 6, 8, 3, 15, Oak);
    box(4, 3, 6, 8, 4, 15, Coral);
    box(3, 2, 6, 5, 7, 15, Coral);
    box(4, 3, 5, 8, 5, 6, Coral);
    box(4, 3, 15, 8, 5, 16, Coral);
    box(5, 4, 7, 7, 6, 9, Gold);
    box(5, 4, 11, 7, 6, 13, Rose);
    box(9, 4, 8, 13, 5, 13, Porcelain);
    for (int z : {8, 12}) {
      box(9, 2, z, 10, 4, z + 1, Oak);
      box(12, 2, z, 13, 4, z + 1, Oak);
    }
    box(10, 5, 9, 12, 6, 11, Teal);
    box(10, 6, 9, 11, 7, 10, Gold);

    // East table: open space under the top, stools and colored ceramics.
    box(21, 5, 9, 28, 6, 16, Oak);
    for (int x : {21, 27}) {
      for (int z : {9, 15}) box(x, 1, z, x + 1, 5, z + 1, Oak);
    }
    for (int z : {7, 17}) {
      box(22, 1, z, 23, 3, z + 2, Oak);
      box(25, 1, z, 26, 3, z + 2, Oak);
      box(21, 3, z, 27, 4, z + 2, Teal);
    }
    box(22, 6, 11, 24, 8, 13, Gold);
    box(22, 8, 11, 23, 9, 12, Gold);
    box(25, 6, 13, 27, 7, 15, Rose);
    box(25, 7, 13, 26, 8, 14, Porcelain);

    // Rear display shelving: real gaps between shelves and colored books.
    box(27, 1, 23, 30, 11, 24, Oak);
    box(27, 1, 29, 30, 11, 30, Oak);
    for (int y : {1, 5, 9}) {
      box(27, y, 23, 30, y + 1, 30, Oak);
      for (int z = 24; z < 29; ++z) {
        const auto color = static_cast<TerrainConfig::Surface>(Coral + (z + y) % 6);
        box(28, y + 1, z, 30, y + 2 + (z % 2), z + 1, color);
      }
    }
    // Stepped sculpture on a white plinth, offset from the central aisle.
    box(7, 1, 23, 12, 3, 28, Plaster);
    box(8, 3, 24, 11, 6, 27, Indigo);
    box(9, 6, 24, 12, 8, 27, Rose);
    box(8, 8, 25, 10, 10, 27, Gold);

    // Two block-built planters with irregular, layered foliage.
    for (const auto &corner : std::array<std::array<int, 2>, 2>{{{4, 19}, {25, 3}}}) {
      const int x = corner[0], z = corner[1];
      box(x, 1, z, x + 3, 3, z + 3, Gold);
      box(x + 1, 3, z + 1, x + 2, 4, z + 2, Soil);
      box(x + 1, 4, z + 1, x + 2, 8, z + 2, Oak);
      box(x - 1, 6, z, x + 3, 8, z + 3, Leaf);
      box(x, 8, z + 1, x + 4, 10, z + 4, Mint);
      box(x + 1, 10, z + 1, x + 3, 11, z + 3, Leaf);
    }

    // Occlusion must see the complete room, including overhangs and cavities.
    for (VoxelBlock &block : blocks) {
      const Vec3 grid = (block.position - base) * (1.0 / config.block_size);
      block.sky_visibility = face_sky(static_cast<int>(std::floor(grid.x)),
                                      static_cast<int>(std::floor(grid.y)),
                                      static_cast<int>(std::floor(grid.z)));
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
