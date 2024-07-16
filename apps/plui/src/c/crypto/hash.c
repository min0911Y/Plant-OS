#include <c.h>

dlexport u32 strnhash(const void *_rest src, size_t len) {
  if (!src) return 0;
  u32 s = 0;
  for (size_t i = 0; i < len; i++)
    s = s * 131 + ((byte *)src)[i];
  return s;
}

dlexport u32 strhash(cstr _rest src) {
  if (!src) return 0;
  u32 s = 0;
  for (; *src; src++)
    s = s * 131 + *(byte *)src;
  return s;
}
