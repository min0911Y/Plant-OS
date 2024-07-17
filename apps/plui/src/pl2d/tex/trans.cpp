#include <pl2d.hpp>

namespace pl2d {

// --------------------------------------------------
//. 简单的绘图函数

template <typename T>
auto BaseTexture<T>::replace(const T &src, const T dst) -> BaseTexture & {
  for (const auto [x, y] : size_rect()) {
    if ((*this)(x, y) == src) (*this)(x, y) = dst;
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::transform(void (*cb)(T &pix)) -> BaseTexture & {
  for (const auto [x, y] : size_rect()) {
    cb((*this)(x, y));
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::transform(void (*cb)(BaseTexture &t, T &pix)) -> BaseTexture & {
  for (const auto [x, y] : size_rect()) {
    cb(*this, (*this)(x, y));
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::transform(void (*cb)(T &pix, i32 x, i32 y)) -> BaseTexture & {
  for (const auto [x, y] : size_rect()) {
    cb((*this)(x, y), x, y);
  }
  return *this;
}

template <typename T>
auto BaseTexture<T>::transform(void (*cb)(BaseTexture &t, T &pix, i32 x, i32 y)) -> BaseTexture & {
  for (const auto [x, y] : size_rect()) {
    cb(*this, (*this)(x, y), x, y);
  }
  return *this;
}

BaseTextureInstantiation

} // namespace pl2d
