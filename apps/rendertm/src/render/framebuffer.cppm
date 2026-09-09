module;

#include "../prelude.hpp"

export module framebuffer;

import math;

export struct RenderBuffers
{
    size_t width = 0;
    size_t height = 0;
    std::vector<float> zbuffer;
    std::vector<LinearColor> sample_colors;
    std::vector<LinearColor> sample_direct;
    std::vector<LinearColor> sample_direct_sun;
    std::vector<LinearColor> sample_direct_moon;
    std::vector<float> shadow_mask_sun;
    std::vector<float> shadow_mask_moon;
    std::vector<float> shadow_mask_filtered_sun;
    std::vector<float> shadow_mask_filtered_moon;
    std::vector<Vec3> sample_normals;
    std::vector<LinearColor> sample_albedo;
    std::vector<LinearColor> sample_indirect;
    std::vector<float> sample_ao;
    std::vector<Vec3> world_positions;
    std::vector<uint32_t> world_stamp;

    auto resize(const size_t new_width, const size_t new_height, const float depth_max) -> bool
    {
        if (width == new_width && height == new_height)
        {
            std::fill(zbuffer.begin(), zbuffer.end(), depth_max);
            return false;
        }

        width = new_width;
        height = new_height;
        const size_t count = width * height;

        const LinearColor black{0.0f, 0.0f, 0.0f};
        const Vec3 zero_vec{0.0, 0.0, 0.0};
        const float val_white = 1.0f;

        auto init_buffer = [&](auto& buffer, const auto& val) {
            buffer.assign(count, val);
        };

        init_buffer(zbuffer, depth_max);
        init_buffer(sample_colors, black);
        init_buffer(sample_direct, black);
        init_buffer(sample_direct_sun, black);
        init_buffer(sample_direct_moon, black);
        init_buffer(sample_indirect, black);
        init_buffer(sample_albedo, black);
        init_buffer(shadow_mask_sun, val_white);
        init_buffer(shadow_mask_moon, val_white);
        init_buffer(shadow_mask_filtered_sun, val_white);
        init_buffer(shadow_mask_filtered_moon, val_white);
        init_buffer(sample_ao, val_white);
        init_buffer(sample_normals, zero_vec);
        init_buffer(world_positions, zero_vec);
        init_buffer(world_stamp, 0);
        return true;
    }
};
