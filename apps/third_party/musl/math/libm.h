/* Native adapter for the imported musl scalar math routines. */
#ifndef PLANT_MUSL_MATH_H
#define PLANT_MUSL_MATH_H
#include <float.h>
#include <limits.h>
#include <math.h>

#define WANT_ROUNDING 1
#define FP_ILOGB0 INT_MIN
#define FP_ILOGBNAN INT_MIN
#if FLT_EVAL_METHOD == 2
#define double_t long double
#define float_t long double
#endif
static inline float eval_as_float(float value) { return value; }
double __expo2(double value, double sign) __attribute__((visibility("hidden")));
float __expo2f(float value, float sign) __attribute__((visibility("hidden")));
int __rem_pio2(double value, double *remainder);
double __sin(double value, double tail, int nonzero_tail);
double __cos(double value, double tail);
#endif
