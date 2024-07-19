#include <dos.h>
#define UINT32_MAX 0xffffffff
#define INT32_MAX  0x7fffffff
static u32 next1 = 1;
u32        rand(void) {
  next1 = next1 * 1103515245 + 12345;
  return ((u32)(next1));
}
void srand(u32 seed) {
  next1 = seed;
}
