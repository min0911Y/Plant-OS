/* The supplied Rust i386 archive returns floating-point values in EAX/EDX.
 * Compile this private bridge with -mno-fp-ret-in-387; arithmetic still uses
 * Plant OS's x87 backend, with no host runtime or SSE dependency. */
#include <stdint.h>

float __addsf3(float a, float b) { return a + b; }
float __subsf3(float a, float b) { return a - b; }
float __mulsf3(float a, float b) { return a * b; }
float __divsf3(float a, float b) { return a / b; }

int __eqsf2(float a, float b) { return a != b; }
int __unordsf2(float a, float b) { return __builtin_isunordered(a, b); }
int __unorddf2(double a, double b) { return __builtin_isunordered(a, b); }

int __lesf2(float a, float b) {
  return __builtin_isunordered(a, b) ? 1 : (a > b) - (a < b);
}
int __gesf2(float a, float b) {
  return __builtin_isunordered(a, b) ? -1 : (a > b) - (a < b);
}
int __gedf2(double a, double b) {
  return __builtin_isunordered(a, b) ? -1 : (a > b) - (a < b);
}
int __ltsf2(float, float) __attribute__((alias("__lesf2")));
int __gtsf2(float, float) __attribute__((alias("__gesf2")));
int __gtdf2(double, double) __attribute__((alias("__gedf2")));

int32_t __fixdfsi(double value) { return (int32_t)value; }
int32_t __fixsfsi(float value) { return (int32_t)value; }
uint32_t __fixunssfsi(float value) { return (uint32_t)value; }
double __floatsidf(int32_t value) { return (double)value; }
float __floatsisf(int32_t value) { return (float)value; }
float __floatunsisf(uint32_t value) { return (float)value; }
float __truncdfsf2(double value) { return (float)value; }

float fminimum_numf(float a, float b) {
  if (__builtin_isnan(a))
    return b;
  if (__builtin_isnan(b))
    return a;
  return a < b || (a == b && __builtin_signbit(a)) ? a : b;
}
float fmaximum_numf(float a, float b) {
  if (__builtin_isnan(a))
    return b;
  if (__builtin_isnan(b))
    return a;
  return a > b || (a == b && !__builtin_signbit(a)) ? a : b;
}
