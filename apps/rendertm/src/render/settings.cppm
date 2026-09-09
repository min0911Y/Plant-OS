module;

#include "../prelude.hpp"

export module settings;

import math;

export struct TaaSettings
{
    bool enabled = true;
    double blend = 0.05;
    bool clamp_enabled = true;
};

export struct GiSettings
{
    bool enabled = false;
    double strength = 0.0;
    int bounce_count = 2;
    double ray_bias = 0.04;
    double max_distance = 12.0;
    int noise_salt = 73;
    int sample_count = 1;
    float clamp = 4.0f;
    float ao_lift = 0.15f;
};

export struct ShadowSettings
{
    double ray_bias = 0.05;
    int sun_salt = 17;
    int moon_salt = 19;
    bool filter_enabled = true;
    float filter_depth_threshold = 1.0f;
    float filter_normal_threshold = 0.5f;
    float filter_center_weight = 4.0f;
    float filter_neighbor_weight = 1.0f;
};

export struct LightingSettings
{
    double sun_intensity_boost = 1.2;
    double hemisphere_bounce_strength = 0.35;
    LinearColor hemisphere_bounce_color{1.0f, 0.9046612f, 0.7758222f};
};

export struct RenderSettings
{
    bool paused = false;
    double ambient_light = 0.13;
    bool ambient_occlusion_enabled = true;
    bool shadow_enabled = true;
    TaaSettings taa{};
    GiSettings gi{};
    ShadowSettings shadow{};
    LightingSettings lighting{};
};
