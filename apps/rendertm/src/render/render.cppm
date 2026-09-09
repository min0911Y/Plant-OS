module;

#include "../prelude.hpp"

export module render;

export import math;
export import execution;
export import camera;
export import world;
export import noise;
export import terrain;
export import settings;
export import lighting;
export import framebuffer;
export import rasterizer;
export import post;

struct FrameState
{
    size_t width = 0;
    size_t height = 0;
    size_t samples = 0;
    float depth_max = 0.0f;
    uint32_t frame_index = 0;

    float jitter_x = 0.0f;
    float jitter_y = 0.0f;

    double proj_scale_x = 0.0;
    double proj_scale_y = 0.0;

    Vec3 camera_pos{};
    YawPitch orientation{};

    FrameLighting lighting{};
    LinearColor hemi_ground{};
    bool direct_on = false;
    bool gi_active = false;
    int gi_bounces = 0;
};

export struct RenderEngine
{
    RenderWorkers workers;
    Camera camera{};
    World world{};
    Terrain terrain{};
    RenderSettings settings{};
    uint32_t renderFrameIndex = 0;
    RenderBuffers buffers{};
    Rasterizer rasterizer{};
    LightingEngine lighting{};
    PostProcessor post{};

    auto update(uint32_t* framebuffer, size_t width, size_t height, size_t pitch = 0) -> void
    {
        const FrameState frame = begin_frame(width, height);
        rasterize_scene(frame);

        const auto& lights = frame.lighting.lights;
        const DirectFrame direct_frame{
            .depth_max = frame.depth_max,
            .frame_index = frame.frame_index,
            .shadows_on = settings.shadow_enabled && frame.direct_on,
            .lights = lights
        };
        lighting.resolve_direct(direct_frame, buffers, terrain, settings.shadow, &workers);

        if (frame.gi_active)
        {
            lighting.gi_pass(GiFrame{
                .width = frame.width,
                .height = frame.height,
                .depth_max = frame.depth_max,
                .frame_index = frame.frame_index,
                .jitter_x = static_cast<double>(frame.jitter_x),
                .jitter_y = static_cast<double>(frame.jitter_y),
                .bounces = frame.gi_bounces,
                .scale = static_cast<float>(std::max(0.0, settings.gi.strength)),
                .sky_scale = frame.lighting.sky_scale,
                .sky_top = frame.lighting.ambient_zenith,
                .hemi_ground = frame.hemi_ground,
                .lights = frame.lighting.lights,
                .inv_current_vp = post.inverseCurrentVP
            }, buffers, terrain, settings.gi, &workers);
        }
        post.resolve_frame(framebuffer, buffers, PostFrame{
            .lighting = frame.lighting,
            .taa_on = settings.taa.enabled,
            .clamp_history = settings.taa.clamp_enabled,
            .taa_factor = static_cast<float>(std::clamp(settings.taa.blend, 0.0, 1.0)),
            .gi_active = frame.gi_active,
            .frame_index = frame.frame_index,
            .jitter_x = frame.jitter_x,
            .jitter_y = frame.jitter_y,
            .exposure = static_cast<float>(std::max(0.0, world.sky.exposure)),
            .camera_pos = frame.camera_pos
        }, world.sky, settings.gi, &workers, pitch);
    }

private:
    static constexpr int kTaaJitterSalt = 37;

    auto begin_frame(const size_t width, const size_t height) -> FrameState
    {
        FrameState frame{};
        frame.width = width;
        frame.height = height;
        frame.samples = width * height;
        frame.depth_max = std::numeric_limits<float>::max();
        frame.frame_index = renderFrameIndex++;

        if (buffers.resize(width, height, frame.depth_max))
        {
            post.resize_buffers(frame.samples);
        }

        world.update_orbits(settings.paused);

        frame.camera_pos = camera.position;
        frame.orientation = camera.orientation();

        post.previousVP = post.currentVP;
        post.update_taa_state(settings.taa.enabled, width, height, frame.samples,
                              camera.position, camera.rotation.x, camera.rotation.y);

        frame.proj_scale_y = static_cast<double>(height) * Camera::fov_scale;
        frame.proj_scale_x = frame.proj_scale_y;
        const Mat4 proj = Camera::projection(static_cast<double>(width),
                                             static_cast<double>(height),
                                             frame.proj_scale_x, frame.proj_scale_y);
        post.currentVP = proj * camera.view_matrix();
        post.inverseCurrentVP = post.currentVP.invert().value_or(Mat4::identity());

        if (settings.taa.enabled)
        {
            const int frame_idx = static_cast<int>(frame.frame_index);
            const float u = BlueNoise::sample(0, 0, frame_idx, kTaaJitterSalt);
            const float v = BlueNoise::sample(1, 0, frame_idx, kTaaJitterSalt + 1);
            frame.jitter_x = u - 0.5f;
            frame.jitter_y = v - 0.5f;
        }

        frame.lighting = world.evaluate_lighting(settings.lighting.sun_intensity_boost);
        const auto& lights = frame.lighting.lights;
        const auto is_active = [](const auto& light) { return light.intensity > 0.0; };
        frame.direct_on = std::any_of(lights.begin(), lights.end(), is_active);
        frame.hemi_ground = lighting.hemi_ground(frame.lighting.ambient_horizon,
                                                 lights, settings.lighting);

        frame.gi_bounces = std::max(0, settings.gi.bounce_count);
        frame.gi_active = settings.gi.enabled && settings.gi.strength > 0.0
                          && frame.gi_bounces > 0;

        return frame;
    }

    auto rasterize_scene(const FrameState& frame) -> void
    {
        if (terrain.blocks.empty() || terrain.mesh.empty())
        {
            terrain.generate();
        }

        std::vector<ShadingContext> contexts;
        contexts.reserve(terrain.config.palette.size());
        for (size_t i = 0; i < terrain.config.palette.size(); ++i)
        {
            contexts.push_back({
                terrain.albedo[i],
                frame.lighting.ambient_zenith,
                frame.hemi_ground,
                frame.lighting.sky_scale,
                frame.camera_pos,
                settings.ambient_light,
                terrain.config.palette[i],
                frame.direct_on,
                settings.ambient_occlusion_enabled,
                frame.lighting.lights
            });
        }

        const RasterTarget target{
            .zbuffer = buffers.zbuffer.data(),
            .sample_colors = buffers.sample_colors.data(),
            .sample_direct_sun = buffers.sample_direct_sun.data(),
            .sample_direct_moon = buffers.sample_direct_moon.data(),
            .sample_normals = buffers.sample_normals.data(),
            .sample_albedo = buffers.sample_albedo.data(),
            .sample_ao = buffers.sample_ao.data(),
            .world_positions = buffers.world_positions.data(),
            .world_stamp = buffers.world_stamp.data(),
            .frame_index = frame.frame_index,
            .width = frame.width,
            .height = frame.height
        };

        for (const auto& quad : terrain.mesh)
        {
            const RasterInputs inputs{
                .ctx = contexts[quad.material],
                .jitter_x = frame.jitter_x,
                .jitter_y = frame.jitter_y
            };
            const RasterQuadInput quad_input{
                .quad = quad,
                .proj_scale_x = frame.proj_scale_x,
                .proj_scale_y = frame.proj_scale_y,
                .camera_pos = frame.camera_pos,
                .orientation = frame.orientation
            };
            rasterizer.render_quad(target, quad_input, inputs);
        }
    }
};
