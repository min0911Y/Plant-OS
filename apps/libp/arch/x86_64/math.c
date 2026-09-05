#include <math.h>

int __rem_pio2(double value, double remainder[2]);
double __sin(double value, double tail, int nonzero_tail);
double __cos(double value, double tail);

double sin(double value) {
  double remainder[2];
  unsigned quadrant = __rem_pio2(value, remainder) & 3;
  switch (quadrant) {
  case 0:
    return __sin(remainder[0], remainder[1], 1);
  case 1:
    return __cos(remainder[0], remainder[1]);
  case 2:
    return -__sin(remainder[0], remainder[1], 1);
  default:
    return -__cos(remainder[0], remainder[1]);
  }
}
double cos(double value) {
  double remainder[2];
  unsigned quadrant = __rem_pio2(value, remainder) & 3;
  switch (quadrant) {
  case 0:
    return __cos(remainder[0], remainder[1]);
  case 1:
    return -__sin(remainder[0], remainder[1], 1);
  case 2:
    return -__cos(remainder[0], remainder[1]);
  default:
    return __sin(remainder[0], remainder[1], 1);
  }
}
double sqrt(double value) {
  double result;
  __asm__("sqrtsd %1, %0" : "=x"(result) : "x"(value));
  return result;
}
