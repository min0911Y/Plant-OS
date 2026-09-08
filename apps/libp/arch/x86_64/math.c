#include <math.h>

long double scalbnl(long double value, int exponent) {
  return scalbn(value, exponent);
}
long double fmodl(long double value, long double divisor) {
  return fmod(value, divisor);
}
long double fabsl(long double value) { return __builtin_fabsl(value); }
long double copysignl(long double value, long double sign) {
  return __builtin_copysignl(value, sign);
}

double sqrt(double value) {
  double result;
  __asm__("sqrtsd %1, %0" : "=x"(result) : "x"(value));
  return result;
}
