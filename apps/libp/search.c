#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
  const unsigned char *first = base;
  while (count) {
    size_t middle = count / 2;
    const void *element = first + middle * size;
    int order = compare(key, element);
    if (!order)
      return (void *)element;
    if (order < 0)
      count = middle;
    else {
      first = (const unsigned char *)element + size;
      count -= middle + 1;
    }
  }
  return NULL;
}
