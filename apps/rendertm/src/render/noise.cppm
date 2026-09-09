module;

#include "../prelude.hpp"

export module noise;

export struct BlueNoise
{
    struct Shift
    {
        uint32_t x;
        uint32_t y;
    };

    [[nodiscard]]
    static constexpr auto shift(int frame, int salt) -> Shift
    {
        static constexpr auto hash32 = [](uint32_t value) -> uint32_t {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        };

        const uint32_t seed = static_cast<uint32_t>(frame) ^
                              (static_cast<uint32_t>(salt) * 0x9e3779b9u);
        return {
            hash32(seed) & static_cast<uint32_t>(kMask),
            hash32(seed ^ 0x85ebca6bu) & static_cast<uint32_t>(kMask)
        };
    }

    [[nodiscard]]
    static constexpr auto sample(int x, int y, const Shift& shift) -> float
    {
        const int nx = (x + static_cast<int>(shift.x)) & kMask;
        const int ny = (y + static_cast<int>(shift.y)) & kMask;
        const int idx = ny * kSize + nx;
        return (static_cast<float>(kData[idx]) + 0.5f) / 256.0f;
    }

    [[nodiscard]]
    static constexpr auto sample(int x, int y, int frame, int salt) -> float
    {
        return sample(x, y, shift(frame, salt));
    }

private:
    static constexpr int kSize = 64;
    static constexpr int kMask = kSize - 1;
    static constexpr unsigned char kData[4096] = {
        #include "blue_noise.inc"
    };
};

export struct SimplexNoise
{
    [[nodiscard]]
    static auto sample(const double xin, const double yin) -> double
    {
        static const std::array<int, 512> perm = []() {
            std::array<int, 256> p{};
            std::iota(p.begin(), p.end(), 0);

            std::mt19937 rng(1337);
            std::shuffle(p.begin(), p.end(), rng);

            std::array<int, 512> result{};
            for (size_t i = 0; i < result.size(); ++i)
            {
                result[i] = p[i & 255];
            }
            return result;
        }();

        static constexpr std::array<std::array<int, 2>, 8> grad2 = {{
            {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
            {{1, 1}}, {{-1, 1}}, {{1, -1}}, {{-1, -1}}
        }};

        static constexpr double f2 = 0.366025403784438646;
        static constexpr double g2 = 0.211324865405187117;

        const double s = (xin + yin) * f2;
        const int i = static_cast<int>(std::floor(xin + s));
        const int j = static_cast<int>(std::floor(yin + s));
        const double t = (i + j) * g2;
        const double x0 = xin - (static_cast<double>(i) - t);
        const double y0 = yin - (static_cast<double>(j) - t);

        const int i1 = (x0 > y0) ? 1 : 0;
        const int j1 = (x0 > y0) ? 0 : 1;

        const double x1 = x0 - static_cast<double>(i1) + g2;
        const double y1 = y0 - static_cast<double>(j1) + g2;
        const double x2 = x0 - 1.0 + 2.0 * g2;
        const double y2 = y0 - 1.0 + 2.0 * g2;

        const int ii = i & 255;
        const int jj = j & 255;

        auto grad_dot = [&](int hash, double x, double y) -> double {
            const auto& g = grad2[hash & 7];
            return static_cast<double>(g[0]) * x + static_cast<double>(g[1]) * y;
        };

        double n0 = 0.0;
        double t0 = 0.5 - x0 * x0 - y0 * y0;
        if (t0 > 0.0)
        {
            t0 *= t0;
            n0 = t0 * t0 * grad_dot(perm[ii + perm[jj]], x0, y0);
        }

        double n1 = 0.0;
        double t1 = 0.5 - x1 * x1 - y1 * y1;
        if (t1 > 0.0)
        {
            t1 *= t1;
            n1 = t1 * t1 * grad_dot(perm[ii + i1 + perm[jj + j1]], x1, y1);
        }

        double n2 = 0.0;
        double t2 = 0.5 - x2 * x2 - y2 * y2;
        if (t2 > 0.0)
        {
            t2 *= t2;
            n2 = t2 * t2 * grad_dot(perm[ii + 1 + perm[jj + 1]], x2, y2);
        }

        return 70.0 * (n0 + n1 + n2);
    }
};
