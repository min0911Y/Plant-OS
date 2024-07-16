#pragma once
#include <cpp.hpp>
#include <define.h>
#include <type.hpp>

// pixel 也可以处理颜色变换等

namespace pl2d {

constexpr auto version = 0;

namespace color {
enum EFmtConv {
  U8,
  U16,
  U32,
  F32,
  F64,
};
enum EColorSpaceConv {
  RGB2Grayscale,
  RGB2HSV,
  HSV2RGB,
  RGB2HSL,
  HSL2RGB,
  RGB2XYZ,
  XYZ2RGB,
  XYZ2LAB,
  LAB2XYZ,
  RGB2LAB,
  LAB2RGB,
  XYZ2LUV,
  LUV2XYZ,
  RGB2LUV,
  LUV2RGB,
};
} // namespace color

dlimport auto gamma_correct(f32 x) -> f32; // 进行 gamma 矫正
dlimport auto gamma_correct(f64 x) -> f64; // 进行 gamma 矫正
dlimport auto reverse_gamma(f32 x) -> f32; // 反向 gamma 矫正
dlimport auto reverse_gamma(f64 x) -> f64; // 反向 gamma 矫正

// BasePixel 结构体的模板，因为有些长就写成宏
#define BasePixelTemplate                                                                          \
  typename T /* 储存颜色值的类型（无符号整数或浮点，有符号当作无符号） */,                         \
      typename T2 /* 直接运算时的类型（至少大一倍防止溢出） */,                                    \
      typename FT /* 转换成浮点数运算时的类型 */,                                                  \
      typename std::conditional_t<std::is_floating_point_v<T>, i32, T>                             \
          T_MAX /* 最大值（浮点设置为 1） */,                                                      \
      typename std::conditional_t<std::is_floating_point_v<T>, i32, T>                             \
          T_MAX_2 /* 对应有符号类型的最大值（浮点设置为 1） */
#define BasePixelT BasePixel<T, T2, FT, T_MAX, T_MAX_2>
// 就是上面的加个下划线
#define _BasePixelTemplate                                                                         \
  typename _T /* 储存颜色值的类型（无符号整数或浮点，有符号当作无符号） */,                        \
      typename _T2 /* 直接运算时的类型（至少大一倍防止溢出） */,                                   \
      typename _FT /* 转换成浮点数运算时的类型 */,                                                 \
      typename std::conditional_t<std::is_floating_point_v<_T>, i32, _T>                           \
          _T_MAX /* 最大值（浮点设置为 1） */,                                                     \
      typename std::conditional_t<std::is_floating_point_v<_T>, i32, _T>                           \
          _T_MAX_2 /* 对应有符号类型的最大值（浮点设置为 1） */
#define _BasePixelT BasePixel<_T, _T2, _FT, _T_MAX, _T_MAX_2>

template <BasePixelTemplate>
struct BasePixel;

// 参数的含义参考 BasePixelTemplate 的定义
#define BasePixelBT BasePixel<u8, u32, f32, U8_MAX, I8_MAX>
#define BasePixelST BasePixel<u16, u32, f32, U16_MAX, I16_MAX>
#define BasePixelIT BasePixel<u32, u64, f64, U32_MAX, I32_MAX>
#define BasePixelFT BasePixel<f32, f32, f32, 1, 1>
#define BasePixelDT BasePixel<f64, f64, f64, 1, 1>

// 显式模板实例化
#define BasePixelInstantiation                                                                     \
  template class BasePixelBT;                                                                      \
  template class BasePixelST;                                                                      \
  template class BasePixelIT;                                                                      \
  template class BasePixelFT;                                                                      \
  template class BasePixelDT;

using PixelB = BasePixelBT; // byte
using PixelS = BasePixelST; // short
using PixelI = BasePixelIT; // int
using PixelF = BasePixelFT; // float
using PixelD = BasePixelDT; // double
using Pixel  = PixelB;

template <BasePixelTemplate>
struct BasePixel {
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

// 部分函数定义

template <BasePixelTemplate>
auto BasePixelT::operator+(const BasePixel &s) const -> BasePixel {
  return BasePixel{
      (T)(r + s.r),
      (T)(g + s.g),
      (T)(b + s.b),
      (T)(a + s.a),
  };
}
template <BasePixelTemplate>
auto BasePixelT::operator+=(const BasePixel &s) -> BasePixel & {
  r += s.r;
  g += s.g;
  b += s.b;
  a += s.a;
  return *this;
}
template <BasePixelTemplate>
auto BasePixelT::operator-(const BasePixel &s) const -> BasePixel {
  return BasePixel{
      (T)(r - s.r),
      (T)(g - s.g),
      (T)(b - s.b),
      (T)(a - s.a),
  };
}
template <BasePixelTemplate>
auto BasePixelT::operator-=(const BasePixel &s) -> BasePixel & {
  r -= s.r;
  g -= s.g;
  b -= s.b;
  a -= s.a;
  return *this;
}
template <BasePixelTemplate>
auto BasePixelT::operator*(f32 s) const -> BasePixel {
  return BasePixel{
      (T)(r * s),
      (T)(g * s),
      (T)(b * s),
      (T)(a * s),
  };
}
template <BasePixelTemplate>
auto BasePixelT::operator*=(f32 s) -> BasePixel & {
  r *= r;
  g *= s;
  b *= s;
  a *= s;
  return *this;
}
template <BasePixelTemplate>
auto BasePixelT::operator/(f32 s) const -> BasePixel {
  return BasePixel{
      (T)(r / s),
      (T)(g / s),
      (T)(b / s),
      (T)(a / s),
  };
}
template <BasePixelTemplate>
auto BasePixelT::operator/=(f32 s) -> BasePixel & {
  r /= r;
  g /= s;
  b /= s;
  a /= s;
  return *this;
}

} // namespace pl2d
