module;

#include "../prelude.hpp"

export module camera;

import math;

export struct Camera {
    Vec3 position{17.42, 26.26, -2.76};
    Vec2 rotation{-0.69, 0.60};

    static constexpr double near_plane = 0.05;
    static constexpr double far_plane = 1000.0;
    static constexpr double max_pitch = 1.4;
    static constexpr double fov_scale = 0.8;

    static constexpr double proj_a = far_plane / (far_plane - near_plane);
    static constexpr double proj_b = -near_plane * far_plane / (far_plane - near_plane);

    [[nodiscard]]
    auto orientation() const -> YawPitch
    {
        return YawPitch::from_angles(rotation.x, rotation.y);
    }

    auto move_local(const Vec3& delta) -> void
    {
        position = position + orientation().apply(delta);
    }

    auto move(const Vec3& intent) -> void
    {
        const YawPitch rot = orientation();
        const YawPitch yaw_only{rot.cy, rot.sy, 1.0, 0.0};
        position = position
                 + rot.apply({0.0, 0.0, intent.z})
                 + yaw_only.apply({intent.x, 0.0, 0.0})
                 + Vec3{0.0, intent.y, 0.0};
    }

    auto rotate(const Vec2& delta) -> void
    {
        set_rotation(rotation + delta);
    }

    auto set_rotation(const Vec2& rot) -> void
    {
        rotation.x = rot.x;
        rotation.y = std::clamp(rot.y, -max_pitch, max_pitch);
    }

    [[nodiscard]]
    auto from_camera_space(const Vec3& view) const -> Vec3
    {
        return position + orientation().apply(view);
    }

    [[nodiscard]]
    auto to_camera_space(const Vec3& world) const -> Vec3
    {
        return orientation().apply_inverse(world - position);
    }

    [[nodiscard]]
    auto view_matrix() const -> Mat4
    {
        const YawPitch rot = orientation();
        const std::array<Vec3, 3> rows{{
            {rot.cy, 0.0, -rot.sy},
            {rot.sy * rot.sp, rot.cp, rot.cy * rot.sp},
            {rot.sy * rot.cp, -rot.sp, rot.cy * rot.cp}
        }};

        Mat4 m = Mat4::identity();
        for (int i = 0; i < 3; ++i)
        {
            m.m[i][0] = rows[i].x;
            m.m[i][1] = rows[i].y;
            m.m[i][2] = rows[i].z;
            m.m[i][3] = -rows[i].dot(position);
        }
        return m;
    }

    [[nodiscard]]
    static auto projection(const double width, const double height,
                           const double proj_scale_x, const double proj_scale_y) -> Mat4
    {
        if (width <= 0.0 || height <= 0.0)
        {
            return Mat4::identity();
        }

        Mat4 m{};
        m.m[0][0] = 2.0 * proj_scale_x / width;
        m.m[1][1] = -2.0 * proj_scale_y / height;
        m.m[2][2] = proj_a;
        m.m[2][3] = proj_b;
        m.m[3][2] = 1.0;
        return m;
    }

    [[nodiscard]]
    static auto screen_to_world(const double screen_x, const double screen_y, const double depth,
                                const Mat4& inv_vp, const double width, const double height) -> Vec3
    {
        const double ndc_x = (screen_x / width - 0.5) * 2.0;
        const double ndc_y = (screen_y / height - 0.5) * 2.0;
        const double ndc_z = proj_a + proj_b / depth;

        const double clip_w = depth;
        const double clip_x = ndc_x * clip_w;
        const double clip_y = ndc_y * clip_w;
        const double clip_z = ndc_z * clip_w;

        double wx = inv_vp.m[0][0] * clip_x + inv_vp.m[0][1] * clip_y + inv_vp.m[0][2] * clip_z + inv_vp.m[0][3] * clip_w;
        double wy = inv_vp.m[1][0] * clip_x + inv_vp.m[1][1] * clip_y + inv_vp.m[1][2] * clip_z + inv_vp.m[1][3] * clip_w;
        double wz = inv_vp.m[2][0] * clip_x + inv_vp.m[2][1] * clip_y + inv_vp.m[2][2] * clip_z + inv_vp.m[2][3] * clip_w;
        const double ww = inv_vp.m[3][0] * clip_x + inv_vp.m[3][1] * clip_y + inv_vp.m[3][2] * clip_z + inv_vp.m[3][3] * clip_w;

        if (std::abs(ww) > 1e-6)
        {
            const double inv_ww = 1.0 / ww;
            wx *= inv_ww;
            wy *= inv_ww;
            wz *= inv_ww;
        }

        return {wx, wy, wz};
    }

    [[nodiscard]]
    static auto world_to_screen(const Mat4& vp, const Vec3& world,
                                const size_t width, const size_t height) -> Vec2
    {
        if (width == 0 || height == 0)
        {
            return {0.0, 0.0};
        }

        const double clip_x = vp.m[0][0] * world.x + vp.m[0][1] * world.y + vp.m[0][2] * world.z + vp.m[0][3];
        const double clip_y = vp.m[1][0] * world.x + vp.m[1][1] * world.y + vp.m[1][2] * world.z + vp.m[1][3];
        const double clip_w = vp.m[3][0] * world.x + vp.m[3][1] * world.y + vp.m[3][2] * world.z + vp.m[3][3];

        if (clip_w <= near_plane)
        {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            return {nan, nan};
        }

        const double inv_w = 1.0 / clip_w;
        return {(clip_x * inv_w * 0.5 + 0.5) * static_cast<double>(width),
                (clip_y * inv_w * 0.5 + 0.5) * static_cast<double>(height)};
    }
};
