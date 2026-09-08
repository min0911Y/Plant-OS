#include <math.h>

long double scalbnl(long double value, int exponent) {
  long double result;
  __asm__("fscale" : "=t"(result) : "0"(value), "u"((long double)exponent));
  return result;
}
long double fmodl(long double value, long double divisor) {
  long double result;
  __asm__("1: fprem; fnstsw %%ax; test $0x400, %%ax; jnz 1b"
          : "=t"(result)
          : "0"(value), "u"(divisor)
          : "ax", "cc");
  return result;
}
long double fabsl(long double value) { return __builtin_fabsl(value); }
long double copysignl(long double value, long double sign) {
  return __builtin_copysignl(value, sign);
}

double sqrt(double value) {
  double result;
  __asm__("fsqrt" : "=t"(result) : "0"(value));
  return result;
}
