#include <c.h>
#include <cpp.hpp>
#include <define.h>
#include <pl2d.hpp>
#include <type.hpp>

namespace pl2d {

template <typename T>
auto BaseTexture<T>::fft_resize(float s) -> BaseTexture & {
  u32 w = width * s, h = height * s;
  return fft_resize(w, h);
}

template <>
auto BaseTexture<PixelF>::fft_resize(u32 w, u32 h) -> BaseTexture & {
  // pl2d::TextureF tmp(image_tex.width, image_tex.height);
  // image_tex.copy_to(tmp);
  // tmp.fft();
  // pl2d::TextureF tmp2(tmp.width * 2, tmp.height * 2);
  // tmp.paste_to(tmp2, 0, 0);
  // tmp2.ift();
  // image_tex = {tmp2.width, tmp2.height};
  // tmp2.copy_to(image_tex);
  return *this;
}

template <typename T>
auto BaseTexture<T>::fft_resize(u32 w, u32 h) -> BaseTexture & {
  // pl2d::TextureF tmp(image_tex.width, image_tex.height);
  // image_tex.copy_to(tmp);
  // tmp.fft();
  // pl2d::TextureF tmp2(tmp.width * 2, tmp.height * 2);
  // tmp.paste_to(tmp2, 0, 0);
  // tmp2.ift();
  // image_tex = {tmp2.width, tmp2.height};
  // tmp2.copy_to(image_tex);
  return *this;
}

template <typename T>
auto BaseTexture<T>::fft_resize_copy() -> BaseTexture * {}

BaseTextureInstantiation

} // namespace pl2d
