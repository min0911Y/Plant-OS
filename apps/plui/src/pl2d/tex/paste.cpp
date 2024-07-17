#include <c.h>
#include <pl2d.hpp>

namespace pl2d {

template <typename T>
auto BaseTexture<T>::paste_from(const BaseTexture<T> &tex, i32 dx, i32 dy) -> BaseTexture & {
  Rect rect = {(i32)tex.width, (i32)tex.height};
  rect.trunc(-dx, -dy, (i32)width - dx - 1, (i32)height - dy - 1);
  for (const auto [x, y] : rect) {
    (*this)(x + dx, y + dy) = tex(x, y);
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::paste_to(BaseTexture &tex, i32 x, i32 y) -> BaseTexture & {
  tex.paste_from(*this, x, y);
  return *this;
}

template <typename T>
auto BaseTexture<T>::paste_to(BaseTexture &tex, i32 x, i32 y) const -> const BaseTexture & {
  tex.paste_from(*this, x, y);
  return *this;
}

template <typename T>
auto BaseTexture<T>::paste_from_mix(const BaseTexture<T> &tex, i32 dx, i32 dy) -> BaseTexture & {
  Rect rect = {(i32)tex.width, (i32)tex.height};
  rect.trunc(-dx, -dy, (i32)width - dx - 1, (i32)height - dy - 1);
  for (const auto [x, y] : rect) {
    (*this)(x + dx, y + dy).mix(tex(x, y));
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::paste_to_mix(BaseTexture &tex, i32 x, i32 y) -> BaseTexture & {
  tex.paste_from_mix(*this, x, y);
  return *this;
}

template <typename T>
auto BaseTexture<T>::paste_to_mix(BaseTexture &tex, i32 x, i32 y) const -> const BaseTexture & {
  tex.paste_from_mix(*this, x, y);
  return *this;
}

BaseTextureInstantiation

} // namespace pl2d
