#include <c.h>
#include <cpp.hpp>
#include <define.h>
#include <pl2d.hpp>
#include <type.hpp>

namespace pl2d {

#if 0
template <typename T>
auto BaseTexture<T>::gaussian_blur(i32 size, f32 sigma) -> BaseTexture & {
  auto *tmp    = (PixelF *)malloc(cpp::max(width, height) * sizeof(PixelF));
  f32  *kernel = (f32 *)malloc(size * sizeof(f32));
  cpp::gaussian_kernel_1(kernel, size, sigma);

  for (i32 y = 0; y < height; y++) {
    for (i32 x = 0; x < width; x++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = cpp::clamp(x - size / 2 + j, 0, (i32)width - 1);
        sum       += (PixelF)(*this)(index, y) * kernel[j];
      }
      tmp[x] = sum;
    }
    for (i32 x = 0; x < width; x++) {
      (*this)(x, y) = tmp[x];
    }
  }

  for (i32 x = 0; x < width; x++) {
    for (i32 y = 0; y < height; y++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = cpp::clamp(y - size / 2 + j, 0, (i32)height - 1);
        sum       += (PixelF)(*this)(x, index) * kernel[j];
      }
      tmp[y] = sum;
    }
    for (i32 y = 0; y < height; y++) {
      (*this)(x, y) = tmp[y];
    }
  }

  free(kernel);
  free(tmp);
  return *this;
}
#else
template <typename T>
auto BaseTexture<T>::gaussian_blur(i32 size, f32 sigma) -> BaseTexture & {
  auto *tmp    = (PixelF *)malloc(cpp::max(width, height) * sizeof(PixelF));
  f32  *kernel = (f32 *)malloc(size * sizeof(f32));
  cpp::gaussian_kernel_1(kernel, size, sigma);

  for (i32 y = 0; y < height; y++) {
    for (i32 x = 0; x < size / 2; x++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = cpp::max(x - size / 2 + j, 0);
        sum       += (PixelF)(*this)(index, y) * kernel[j];
      }
      tmp[x] = sum;
    }
    for (i32 x = size / 2; x < width - size / 2; x++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = x - size / 2 + j;
        sum       += (PixelF)(*this)(index, y) * kernel[j];
      }
      tmp[x] = sum;
    }
    for (i32 x = width - size / 2; x < width; x++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = cpp::min(x - size / 2 + j, (i32)width - 1);
        sum       += (PixelF)(*this)(index, y) * kernel[j];
      }
      tmp[x] = sum;
    }
    for (i32 x = 0; x < width; x++) {
      (*this)(x, y) = tmp[x];
    }
  }

  for (i32 x = 0; x < width; x++) {
    for (i32 y = 0; y < size / 2; y++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = cpp::clamp(y - size / 2 + j, 0, (i32)height - 1);
        sum       += (PixelF)(*this)(x, index) * kernel[j];
      }
      tmp[y] = sum;
    }
    for (i32 y = size / 2; y < height - size / 2; y++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = y - size / 2 + j;
        sum       += (PixelF)(*this)(x, index) * kernel[j];
      }
      tmp[y] = sum;
    }
    for (i32 y = height - size / 2; y < height; y++) {
      PixelF sum = 0;
      for (i32 j = 0; j < size; j++) {
        int index  = cpp::clamp(y - size / 2 + j, 0, (i32)height - 1);
        sum       += (PixelF)(*this)(x, index) * kernel[j];
      }
      tmp[y] = sum;
    }
    for (i32 y = 0; y < height; y++) {
      (*this)(x, y) = tmp[y];
    }
  }

  free(kernel);
  free(tmp);
  return *this;
}
#endif

BaseTextureInstantiation

} // namespace pl2d
