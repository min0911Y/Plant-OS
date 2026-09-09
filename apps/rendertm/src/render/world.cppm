module;

#include "../prelude.hpp"

export module world;

import math;
import noise;

export struct CelestialDisk
{
    double radius = 0.0;
    double radiance = 0.0;
    LinearColor color{1.0f, 1.0f, 1.0f};
};

export struct DirectionalLight
{
    Vec3 dir{0.0, 1.0, 0.0};
    double intensity = 0.0;
    LinearColor color{1.0f, 1.0f, 1.0f};
    double angular_radius = 0.0;
    CelestialDisk disk{};
};

export struct FrameLighting
{
    std::array<DirectionalLight, 2> lights{};
    LinearColor sky_zenith{};
    LinearColor sky_horizon{};
    LinearColor ambient_zenith{};
    LinearColor ambient_horizon{};
    float sky_scale = 0.0f;
    float star_visibility = 0.0f;
};

export struct Celestial
{
    Vec3 direction{0.0, 0.0, 0.0};
    LinearColor color{1.0f, 1.0f, 1.0f};
    double intensity = 0.0;
    double angular_radius = 0.0;
    CelestialDisk disk{};
    LinearColor disk_zenith_color{1.0f, 1.0f, 1.0f};
    bool orbit_enabled = false;
    double orbit_angle = 0.0;
    double orbit_speed = 0.0;
    double orbit_latitude_deg = 0.0;
    double night_length_ratio = 0.25;

    static constexpr double kPi = std::numbers::pi_v<double>;
    static constexpr double kTau = std::numbers::pi_v<double> * 2.0;

    auto update_orbit(const bool paused) -> void
    {
        if (!orbit_enabled)
        {
            return;
        }
        if (!paused)
        {
            orbit_angle += orbit_speed;
            orbit_angle = std::fmod(orbit_angle, kTau);
            if (orbit_angle < 0.0)
            {
                orbit_angle += kTau;
            }
        }
        direction = direction_at(orbit_angle);
    }

    [[nodiscard]]
    auto height_factor(const Vec3& dir) const -> double
    {
        const double latitude_rad = orbit_latitude_deg * kPi / 180.0;
        const double max_y = std::cos(latitude_rad);
        if (max_y <= 0.0)
        {
            return 0.0;
        }
        const double height = dir.y > 0.0 ? dir.y / max_y : 0.0;
        return std::clamp(height, 0.0, 1.0);
    }

    [[nodiscard]]
    auto height_signed(const Vec3& dir) const -> double
    {
        const double latitude_rad = orbit_latitude_deg * kPi / 180.0;
        const double max_y = std::cos(latitude_rad);
        if (max_y <= 0.0)
        {
            return 0.0;
        }
        return std::clamp(dir.y / max_y, -1.0, 1.0);
    }

    [[nodiscard]]
    auto direction_at(const double angle) const -> Vec3
    {
        const double latitude_rad = orbit_latitude_deg * kPi / 180.0;
        const double max_alt = kPi * 0.5 - latitude_rad;
        const double phase = orbit_phase(angle);
        const double alt = max_alt * std::sin(phase);
        const double az = phase - kPi * 0.5;
        const double cos_alt = std::cos(alt);
        return Vec3{cos_alt * std::sin(az), std::sin(alt), cos_alt * std::cos(az)}.normalize();
    }

    [[nodiscard]]
    auto visible_disk(const Vec3& dir) const -> CelestialDisk
    {
        if (intensity <= 0.0 || disk.radius <= 0.0 || disk.radiance <= 0.0)
        {
            return {};
        }
        const float height = static_cast<float>(dir.y);
        const float radius = static_cast<float>(disk.radius);
        const float visibility = Scalar::smoothstep(-radius, radius, height);
        const float altitude = Scalar::smoothstep(0.0f, 1.0f, height);
        return {
            .radius = disk.radius,
            .radiance = disk.radiance * visibility,
            .color = LinearColor::lerp(disk.color, disk_zenith_color, altitude)
        };
    }

private:
    [[nodiscard]]
    auto orbit_phase(const double angle) const -> double
    {
        double wrapped = std::fmod(angle, kTau);
        if (wrapped < 0.0)
        {
            wrapped += kTau;
        }
        const double night_ratio = std::max(0.0, night_length_ratio);
        const double day_ratio = 1.0;
        const double total = day_ratio + night_ratio;
        if (total <= std::numeric_limits<double>::epsilon())
        {
            return wrapped;
        }
        const double day_fraction = day_ratio / total;
        const double night_fraction = night_ratio / total;
        const double t = wrapped / kTau;
        if (night_ratio <= std::numeric_limits<double>::epsilon())
        {
            return t * kPi;
        }
        if (t < day_fraction)
        {
            return (t / day_fraction) * kPi;
        }
        const double nt = (t - day_fraction) / night_fraction;
        return kPi + nt * kPi;
    }
};

export struct Skybox
{
    struct Gradient
    {
        LinearColor zenith;
        LinearColor horizon;
    };

    struct State
    {
        LinearColor zenith;
        LinearColor horizon;
        float intensity = 0.0f;
        float sun_height = 1.0f;
    };

    double sky_light_scale = 0.55;
    double sun_weight = 0.5;
    double moon_ambient_floor = 0.0;
    double exposure = 1.0;

    LinearColor day_zenith = ColorSrgb::from_hex(0xFF6FB7FF).to_linear();
    LinearColor day_horizon = ColorSrgb::from_hex(0xFFBFDFFF).to_linear();
    LinearColor golden_zenith = ColorSrgb::from_hex(0xFFF2E0C8).to_linear();
    LinearColor golden_horizon = ColorSrgb::from_hex(0xFFE2C299).to_linear();
    LinearColor dawn_zenith = ColorSrgb::from_hex(0xFFE09555).to_linear();
    LinearColor dawn_horizon = ColorSrgb::from_hex(0xFFB85C2E).to_linear();
    LinearColor blue_zenith = ColorSrgb::from_hex(0xFF4B3F7A).to_linear();
    LinearColor blue_horizon = ColorSrgb::from_hex(0xFF262B52).to_linear();
    LinearColor night_zenith = ColorSrgb::from_hex(0xFF090C17).to_linear();
    LinearColor night_horizon = ColorSrgb::from_hex(0xFF04060C).to_linear();
    LinearColor ambient_night_zenith = ColorSrgb::from_hex(0xFF1A2236).to_linear();
    LinearColor ambient_night_horizon = ColorSrgb::from_hex(0xFF0E1424).to_linear();

    double golden_height = 0.7;
    double golden_end = 0.25;
    double blue_height = -0.25;
    double night_height = -0.6;

    double dusk_light_ratio = 0.75;
    double blue_hour_light_ratio = 0.60;
    double night_light_ratio = 0.55;
    double midnight_light_ratio = 0.40;
    double star_small_threshold = 0.96;
    double star_large_threshold = 0.98;
    double star_glow_scale = 1.4;
    double star_fine_scale = 180.0;
    double star_big_scale = 45.0;
    double star_big_offset_u = 17.0;
    double star_big_offset_v = 29.0;
    double star_tint_r = 0.95;
    double star_tint_g = 0.98;
    double star_tint_b = 1.05;
    double celestial_glow_extent = 5.0;
    double celestial_glow_strength = 0.12;

    [[nodiscard]]
    auto sample(const float sun_height) const -> std::pair<LinearColor, LinearColor>
    {
        const float h = std::clamp(sun_height, -1.0f, 1.0f);
        const float golden = static_cast<float>(golden_height);
        const float golden_floor = static_cast<float>(golden_end);
        const float golden_hi = std::clamp(std::max(golden, golden_floor), 0.0f, 1.0f);
        const float golden_lo = std::clamp(std::min(golden, golden_floor), 0.0f, golden_hi);
        const float blue = static_cast<float>(blue_height);
        const float night = static_cast<float>(night_height);

        if (h >= golden_hi)
        {
            return {day_zenith, day_horizon};
        }
        if (h >= golden_lo)
        {
            const float t = Scalar::smoothstep(golden_lo, golden_hi, h);
            return {LinearColor::lerp(golden_zenith, day_zenith, t),
                    LinearColor::lerp(golden_horizon, day_horizon, t)};
        }
        if (h >= 0.0f)
        {
            const float t = Scalar::smoothstep(0.0f, golden_lo, h);
            return {LinearColor::lerp(dawn_zenith, golden_zenith, t),
                    LinearColor::lerp(dawn_horizon, golden_horizon, t)};
        }
        if (h >= blue)
        {
            const float t = Scalar::smoothstep(blue, 0.0f, h);
            return {LinearColor::lerp(blue_zenith, dawn_zenith, t),
                    LinearColor::lerp(blue_horizon, dawn_horizon, t)};
        }
        if (h >= night)
        {
            const float t = Scalar::smoothstep(night, blue, h);
            return {LinearColor::lerp(night_zenith, blue_zenith, t),
                    LinearColor::lerp(night_horizon, blue_horizon, t)};
        }
        return {night_zenith, night_horizon};
    }

    [[nodiscard]]
    auto intensity(const float sun_height, const float moon_intensity) const -> float
    {
        const float h = std::clamp(sun_height, -1.0f, 1.0f);
        const float golden = static_cast<float>(golden_height);
        const float blue = static_cast<float>(blue_height);
        const float night = static_cast<float>(night_height);
        const float dusk_base = static_cast<float>(dusk_light_ratio);
        const float blue_base = static_cast<float>(blue_hour_light_ratio);
        const float night_base = static_cast<float>(night_light_ratio);
        const float midnight_base = static_cast<float>(midnight_light_ratio);

        float base = 1.0f;
        if (h >= golden)
        {
            base = 1.0f;
        }
        else if (h >= 0.0f)
        {
            float t = Scalar::smoothstep(0.0f, golden, h);
            const double power = sun_weight <= 0.0 ? 1.0 : sun_weight;
            t = static_cast<float>(std::pow(t, power));
            base = std::lerp(dusk_base, 1.0f, t);
        }
        else if (h >= blue)
        {
            const float t = Scalar::smoothstep(blue, 0.0f, h);
            base = std::lerp(blue_base, dusk_base, t);
        }
        else if (h >= night)
        {
            const float t = Scalar::smoothstep(night, blue, h);
            base = std::lerp(night_base, blue_base, t);
        }
        else
        {
            const float t = Scalar::smoothstep(-1.0f, night, h);
            base = std::lerp(midnight_base, night_base, t);
        }

        float scale = static_cast<float>(sky_light_scale) * base;
        if (moon_intensity > 0.0f)
        {
            const float moon = std::clamp(moon_intensity, 0.0f, 1.0f);
            scale += moon * static_cast<float>(moon_ambient_floor) * moon_weight(h);
        }
        return std::clamp(scale, 0.0f, 1.0f);
    }

    [[nodiscard]]
    auto moon_weight(const float sun_height) const -> float
    {
        const float h = std::clamp(sun_height, -1.0f, 1.0f);
        const float golden = static_cast<float>(golden_height);
        if (golden <= 0.0f)
        {
            return h > 0.0f ? 0.0f : 1.0f;
        }
        const float t = Scalar::smoothstep(0.0f, golden, std::max(h, 0.0f));
        return 1.0f - t;
    }

    [[nodiscard]]
    auto star_visibility(const float sun_height) const -> float
    {
        const float h = std::clamp(sun_height, -1.0f, 1.0f);
        const float blue = static_cast<float>(blue_height);
        const float night = static_cast<float>(night_height);
        if (h >= blue)
        {
            return 0.0f;
        }
        if (h <= night)
        {
            return 1.0f;
        }
        return Scalar::smoothstep(blue, night, h);
    }

    [[nodiscard]]
    auto ambient_gradient(const float sun_height) const -> Gradient
    {
        const auto [zenith, horizon] = sample(sun_height);
        const float t = star_visibility(sun_height);
        return {LinearColor::lerp(zenith, ambient_night_zenith, t),
                LinearColor::lerp(horizon, ambient_night_horizon, t)};
    }

    [[nodiscard]]
    auto shade(const LinearColor& base, const Vec3& view_dir,
               const FrameLighting& lighting) const -> LinearColor
    {
        LinearColor sky = base;
        const float star_visibility = std::clamp(lighting.star_visibility, 0.0f, 1.0f);
        if (star_visibility > 0.0f)
        {
            const float star = star_intensity(view_dir) * star_visibility
                               * static_cast<float>(star_glow_scale);
            sky = sky + LinearColor{
                star * static_cast<float>(star_tint_r),
                star * static_cast<float>(star_tint_g),
                star * static_cast<float>(star_tint_b)
            };
        }

        const double glow_extent = std::max(1.0, celestial_glow_extent);
        const double glow_extent_sq = glow_extent * glow_extent;
        const float glow_strength = static_cast<float>(std::max(0.0, celestial_glow_strength));
        for (const DirectionalLight& light : lighting.lights)
        {
            if (light.disk.radius <= 0.0 || light.disk.radiance <= 0.0)
            {
                continue;
            }
            const double radius_sq = light.disk.radius * light.disk.radius;
            const double separation_sq =
                2.0 * (1.0 - std::clamp(view_dir.dot(light.dir), -1.0, 1.0));
            const double glow_radius_sq = radius_sq * glow_extent_sq;
            if (separation_sq > glow_radius_sq)
            {
                continue;
            }

            float glow = 1.0f - Scalar::smoothstep(
                static_cast<float>(radius_sq), static_cast<float>(glow_radius_sq),
                static_cast<float>(separation_sq));
            glow *= glow;
            const float disk = separation_sq <= radius_sq ? 1.0f : 0.0f;
            const float radiance = static_cast<float>(light.disk.radiance)
                                   * (disk + glow * glow_strength);
            sky = sky + light.disk.color * radiance;
        }
        return sky;
    }

    [[nodiscard]]
    auto state(const float sun_height, const float moon_intensity) const -> State
    {
        const auto [zenith, horizon] = sample(sun_height);
        return {zenith, horizon, intensity(sun_height, moon_intensity), sun_height};
    }

private:
    [[nodiscard]]
    auto star_intensity(const Vec3& dir) const -> float
    {
        constexpr double pi = std::numbers::pi_v<double>;
        const double u = std::atan2(dir.x, dir.z) / (2.0 * pi);
        const double v = std::asin(std::clamp(dir.y, -1.0, 1.0)) / pi;
        const double n_fine = SimplexNoise::sample(u * star_fine_scale, v * star_fine_scale);
        const double n_big = SimplexNoise::sample(u * star_big_scale + star_big_offset_u,
                                                  v * star_big_scale + star_big_offset_v);
        const float fine = static_cast<float>(n_fine * 0.5 + 0.5);
        const float big = static_cast<float>(n_big * 0.5 + 0.5);
        const float small_star = Scalar::smoothstep(static_cast<float>(star_small_threshold),
                                                    1.0f, fine);
        const float large_star = Scalar::smoothstep(static_cast<float>(star_large_threshold),
                                                    1.0f, big);
        return std::max(small_star, large_star);
    }
};

export struct World
{
    Celestial sun{
        .direction = {0.0, 0.0, -1.0},
        .color = {1.0f, 0.94f, 0.88f},
        .intensity = 1.1,
        .angular_radius = 0.03,
        .disk = {
            .radius = 0.03,
            .radiance = 8.0,
            .color = ColorSrgb::from_hex(0xFFFFA83D).to_linear()
        },
        .disk_zenith_color = {1.0f, 1.0f, 1.0f},
        .orbit_enabled = true,
        .orbit_angle = Celestial::kPi * 0.5,
        .orbit_speed = 0.00075,
        .orbit_latitude_deg = 30.0
    };

    Celestial moon{
        .direction = {0.0, -1.0, 0.0},
        .color = {1.0f, 1.0f, 1.0f},
        .intensity = 0.006,
        .angular_radius = 0.0,
        .disk = {
            .radius = 0.025,
            .radiance = 1.5,
            .color = ColorSrgb::from_hex(0xFFDCE4EE).to_linear()
        },
        .disk_zenith_color = ColorSrgb::from_hex(0xFFDCE4EE).to_linear(),
        .orbit_enabled = false,
        .orbit_angle = 0.0,
        .orbit_speed = 0.0,
        .orbit_latitude_deg = 0.0
    };

    Skybox sky{};

    auto update_orbits(const bool paused) -> void
    {
        sun.update_orbit(paused);
        moon.update_orbit(paused);
    }

    [[nodiscard]]
    auto evaluate_lighting(const double sun_intensity_boost) const -> FrameLighting
    {
        Vec3 sun_dir = sun.direction.normalize();
        double sun_intensity = sun.intensity;
        if (sun.orbit_enabled)
        {
            sun_dir = sun.direction_at(sun.orbit_angle);
            sun_intensity *= sun.height_factor(sun_dir);
        }
        sun_intensity *= sun_intensity_boost;

        Vec3 moon_dir = moon.orbit_enabled ? moon.direction_at(moon.orbit_angle) : -sun_dir;
        moon_dir = (moon_dir.x == 0.0 && moon_dir.y == 0.0 && moon_dir.z == 0.0)
                       ? moon.direction.normalize()
                       : moon_dir.normalize();
        double moon_intensity = std::max(0.0, moon.intensity) * moon.height_factor(moon_dir);

        const float sun_height = static_cast<float>(sun.height_signed(sun_dir));
        const float sky_height = sun.orbit_enabled ? sun_height : 1.0f;
        const auto state = sky.state(sky_height, static_cast<float>(moon_intensity));
        const auto ambient = sky.ambient_gradient(sky_height);
        moon_intensity *= static_cast<double>(sky.moon_weight(sun_height));

        const auto sun_light = DirectionalLight{
            .dir = sun_dir,
            .intensity = sun_intensity,
            .color = sun.color,
            .angular_radius = sun.angular_radius,
            .disk = sun.visible_disk(sun_dir)
        };
        const auto moon_light = DirectionalLight{
            .dir = moon_dir,
            .intensity = moon_intensity,
            .color = moon.color,
            .angular_radius = moon.angular_radius,
            .disk = moon.visible_disk(moon_dir)
        };

        return {
            .lights = {sun_light, moon_light},
            .sky_zenith = state.zenith,
            .sky_horizon = state.horizon,
            .ambient_zenith = ambient.zenith,
            .ambient_horizon = ambient.horizon,
            .sky_scale = state.intensity,
            .star_visibility = sky.star_visibility(sun_height),
        };
    }
};
