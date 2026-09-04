#include <math.h>

double sin(double value) {
  double result;
  __asm__("fsin" : "=t"(result) : "0"(value));
  return result;
}

double cos(double value) {
  double result;
  __asm__("fcos" : "=t"(result) : "0"(value));
  return result;
}

double sqrt(double value) {
  double result;
  __asm__("fsqrt" : "=t"(result) : "0"(value));
  return result;
}
