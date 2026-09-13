#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>

int vsnprintf(char *buffer, size_t capacity, const char *format,
              va_list arguments);

int __vsnprintf_chk(char *buffer, size_t capacity, int flags,
                    size_t object_size, const char *format,
                    va_list arguments) {
  (void)flags;
  (void)object_size;
  return vsnprintf(buffer, capacity, format, arguments);
}

int __snprintf_chk(char *buffer, size_t capacity, int flags,
                   size_t object_size, const char *format, ...) {
  (void)flags;
  (void)object_size;
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(buffer, capacity, format, arguments);
  va_end(arguments);
  return result;
}

void __stack_chk_fail(void) __attribute__((noreturn));
void __stack_chk_fail(void) { abort(); }

off_t lseek64(int descriptor, off_t offset, int whence) {
  return lseek(descriptor, offset, whence);
}

double hypot(double x, double y) {
  x = fabs(x);
  y = fabs(y);
  if (x < y) {
    double swap = x;
    x = y;
    y = swap;
  }
  if (x == 0 || x == HUGE_VAL) {
    return x;
  }
  double ratio = y / x;
  return x * sqrt(1 + ratio * ratio);
}

float hypotf(float x, float y) {
  x = fabsf(x);
  y = fabsf(y);
  if (x < y) {
    float swap = x;
    x = y;
    y = swap;
  }
  if (x == 0 || x == HUGE_VALF) {
    return x;
  }
  float ratio = y / x;
  return x * sqrtf(1 + ratio * ratio);
}
