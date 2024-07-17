#pragma once
#include "pixel.hpp"

namespace pl2d {

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
