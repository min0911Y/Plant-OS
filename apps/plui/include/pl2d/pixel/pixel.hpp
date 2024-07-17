#pragma once
#include "define.hpp"
#include "using.hpp"
#include <cpp.hpp>
#include <define.h>
#include <type.hpp>

// pixel 也可以处理颜色变换等

namespace pl2d {

dlimport auto gamma_correct(f32 x) -> f32; // 进行 gamma 矫正
dlimport auto gamma_correct(f64 x) -> f64; // 进行 gamma 矫正
dlimport auto reverse_gamma(f32 x) -> f32; // 反向 gamma 矫正
dlimport auto reverse_gamma(f64 x) -> f64; // 反向 gamma 矫正

template <BasePixelTemplate>
struct BasePixel {
  using TYPE                       = T;
  using TYPE2                      = T2;
  using FLT_TYPE                   = FT;
  static constexpr auto TYPE_MAX   = T_MAX;
  static constexpr auto TYPE_MAX_2 = T_MAX_2;

  union {
    struct {
#if COLOR_USE_BGR
      T b = 0, g = 0, r = 0, a = 0;
#else
      T r = 0, g = 0, b = 0, a = 0;
#endif
    };
    T d[4]; // 已废弃
  };

  BasePixel() = default;
  BasePixel(u32); // 0xRRGGBBAA 的格式来初始化颜色（仅当定义了 COLOR_READABLE_HEX）
  BasePixel(T r, T g, T b) : r(r), g(g), b(b), a(T_MAX) {}
  BasePixel(T r, T g, T b, T a) : r(r), g(g), b(b), a(a) {}
  BasePixel(const BasePixel &)                         = default;
  BasePixel(BasePixel &&) noexcept                     = default;
  auto operator=(const BasePixel &) -> BasePixel     & = default;
  auto operator=(BasePixel &&) noexcept -> BasePixel & = default;

  template <_BasePixelTemplate>
  BasePixel(const _BasePixelT &p);

  operator u32();

  auto operator==(const BasePixel &p) const -> bool {
    return r == p.r && g == p.g && b == p.b && a == p.a;
  }

  // 已废弃
  auto operator[](size_t n) const -> T {
    return d[n];
  }
  // 已废弃
  auto operator[](size_t n) -> T & {
    return d[n];
  }

  // 元素逐个运算
  auto operator+(const BasePixel &s) const -> BasePixel;
  auto operator+=(const BasePixel &s) -> BasePixel &;
  auto operator-(const BasePixel &s) const -> BasePixel;
  auto operator-=(const BasePixel &s) -> BasePixel &;
  auto operator*(f32 s) const -> BasePixel;
  auto operator*=(f32 s) -> BasePixel &;
  auto operator/(f32 s) const -> BasePixel;
  auto operator/=(f32 s) -> BasePixel &;

  // 计算颜色的差值
  auto diff(const BasePixel &p) -> T {
    T dr = cpp::diff(r, p.r);
    T dg = cpp::diff(g, p.g);
    T db = cpp::diff(b, p.b);
    return cpp::max(dr, dg, db);
  }

  // 按比例进行颜色混合函数
  void        mix_ratio(const BasePixel &s, T k);
  static auto mix_ratio(const BasePixel &c1, const BasePixel &c2, T k) -> BasePixel;
  static auto mix_ratio(const BasePixel &c1, const BasePixel &c2, f32 k) -> BasePixel
  requires(!std::is_same_v<T, f32>);
  // 背景色不透明的混合函数
  void        mix_opaque(const BasePixel &s);
  static auto mix_opaque(const BasePixel &c1, const BasePixel &c2) -> BasePixel;
  // 通用的混合函数
  void        mix(const BasePixel &s);
  static auto mix(const BasePixel &c1, const BasePixel &c2) -> BasePixel;

  auto brightness() const -> T;        // 亮度
  auto grayscale() const -> BasePixel; // 也是亮度，但填充成完整的 rgba

  auto gamma_correct() const -> BasePixel; // 进行 gamma 矫正
  auto reverse_gamma() const -> BasePixel; // 反向 gamma 矫正

  // 已废弃的转换函数
  auto to_u8() const -> PixelB {
    return *this;
  }
  // 已废弃的转换函数
  auto to_u16() const -> PixelS {
    return *this;
  }
  // 已废弃的转换函数
  auto to_u32() const -> PixelI {
    return *this;
  }
  // 已废弃的转换函数
  auto to_f32() const -> PixelF {
    return *this;
  }
  // 已废弃的转换函数
  auto to_f64() const -> PixelD {
    return *this;
  }

  void RGB2Grayscale();
  void RGB2HSV();
  void HSV2RGB();
  void RGB2HSL();
  void HSL2RGB();
  void RGB2XYZ();
  void XYZ2RGB();
  void XYZ2LAB();
  void LAB2XYZ();
  void RGB2LAB();
  void LAB2RGB();
  void XYZ2LUV();
  void LUV2XYZ();
  void RGB2LUV();
  void LUV2RGB();
};

} // namespace pl2d
