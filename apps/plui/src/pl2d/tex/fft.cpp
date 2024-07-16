#include <c.h>
#include <cpp.hpp>
#include <define.h>
#include <pl2d.hpp>
#include <type.hpp>

namespace pl2d {

template <typename T>
auto BaseTexture<T>::fft() -> BaseTexture & {
  if (cpp::max(width, height) <= 4096) {
    pl2d::TextureF tmp(width, height);
    copy_to(tmp);
    tmp.fft();
    copy_from(tmp);
  } else {
    pl2d::TextureD tmp(width, height);
    copy_to(tmp);
    tmp.fft();
    copy_from(tmp);
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::ift() -> BaseTexture & {
  if (cpp::max(width, height) <= 4096) {
    pl2d::TextureF tmp(width, height);
    copy_to(tmp);
    tmp.ift();
    copy_from(tmp);
  } else {
    pl2d::TextureD tmp(width, height);
    copy_to(tmp);
    tmp.ift();
    copy_from(tmp);
  }
  return *this;
}

template <>
auto BaseTexture<PixelF>::fft() -> BaseTexture & {
  auto *x = (f32 *)pixels;
  for (size_t i = 0; i < height; i++) {
    fftf_r2r_p(x + pitch * 4 * i, x + pitch * 4 * i, width, 4, false);
    fftf_r2r_p(x + pitch * 4 * i + 1, x + pitch * 4 * i + 1, width, 4, false);
    fftf_r2r_p(x + pitch * 4 * i + 2, x + pitch * 4 * i + 2, width, 4, false);
    fftf_r2r_p(x + pitch * 4 * i + 3, x + pitch * 4 * i + 3, width, 4, false);
  }
  for (size_t i = 0; i < width * 4; i++) {
    fftf_r2r_p(x + i, x + i, height, pitch * 4, false);
  }
  return *this;
}

template <>
auto BaseTexture<PixelF>::ift() -> BaseTexture & {
  auto *x = (f32 *)pixels;
  for (size_t i = 0; i < height; i++) {
    fftf_r2r_p(x + pitch * 4 * i, x + pitch * 4 * i, width, 4, true);
    fftf_r2r_p(x + pitch * 4 * i + 1, x + pitch * 4 * i + 1, width, 4, true);
    fftf_r2r_p(x + pitch * 4 * i + 2, x + pitch * 4 * i + 2, width, 4, true);
    fftf_r2r_p(x + pitch * 4 * i + 3, x + pitch * 4 * i + 3, width, 4, true);
  }
  for (size_t i = 0; i < width * 4; i++) {
    fftf_r2r_p(x + i, x + i, height, pitch * 4, true);
  }
  return *this;
}

template <>
auto BaseTexture<PixelD>::fft() -> BaseTexture & {
  auto *x = (f64 *)pixels;
  for (size_t i = 0; i < height; i++) {
    fft_r2r_p(x + pitch * 4 * i, x + pitch * 4 * i, width, 4, false);
    fft_r2r_p(x + pitch * 4 * i + 1, x + pitch * 4 * i + 1, width, 4, false);
    fft_r2r_p(x + pitch * 4 * i + 2, x + pitch * 4 * i + 2, width, 4, false);
    fft_r2r_p(x + pitch * 4 * i + 3, x + pitch * 4 * i + 3, width, 4, false);
  }
  for (size_t i = 0; i < width * 4; i++) {
    fft_r2r_p(x + i, x + i, height, pitch * 4, false);
  }
  return *this;
}

template <>
auto BaseTexture<PixelD>::ift() -> BaseTexture & {
  auto *x = (f64 *)pixels;
  for (size_t i = 0; i < height; i++) {
    fft_r2r_p(x + pitch * 4 * i, x + pitch * 4 * i, width, 4, true);
    fft_r2r_p(x + pitch * 4 * i + 1, x + pitch * 4 * i + 1, width, 4, true);
    fft_r2r_p(x + pitch * 4 * i + 2, x + pitch * 4 * i + 2, width, 4, true);
    fft_r2r_p(x + pitch * 4 * i + 3, x + pitch * 4 * i + 3, width, 4, true);
  }
  for (size_t i = 0; i < width * 4; i++) {
    fft_r2r_p(x + i, x + i, height, pitch * 4, true);
  }
  return *this;
}

BaseTextureInstantiation

} // namespace pl2d
