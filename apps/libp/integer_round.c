#include <fenv.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

_Static_assert(DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "integer rounding requires IEEE binary64");

/* Round the significand directly: ties go away from zero, independently of
 * the current FP mode, without raising FE_INEXACT or converting out of range.
 */
static long long integer_round(double value, unsigned width) {
  union {
    double value;
    uint64_t bits;
  } number = {value};
  const unsigned fraction_bits = DBL_MANT_DIG - 1, bias = DBL_MAX_EXP - 1;
  const uint64_t implicit = UINT64_C(1) << fraction_bits;
  const uint64_t limit = UINT64_C(1) << (width - 1);
  unsigned exponent = (number.bits >> fraction_bits) & 0x7ff;
  bool negative = number.bits >> 63;
  uint64_t fraction = number.bits & (implicit - 1);
  uint64_t magnitude;
  if (exponent < bias - 1)
    return 0;
  if (exponent == bias - 1) {
    magnitude = 1;
  } else if (exponent < bias + fraction_bits) {
    unsigned shift = bias + fraction_bits - exponent;
    magnitude = (implicit + fraction + (UINT64_C(1) << (shift - 1))) >> shift;
  } else if (exponent < bias + 63) {
    magnitude = (implicit + fraction) << (exponent - bias - fraction_bits);
  } else {
    if (width == 64 && exponent == bias + 63 && !fraction && negative)
      return LLONG_MIN;
    goto invalid;
  }
  if (magnitude > limit - !negative)
    goto invalid;
  return negative ? -(long long)magnitude : (long long)magnitude;
invalid:
  feraiseexcept(FE_INVALID);
  return -(long long)(limit - 1) - 1;
}

long lround(double value) {
  return integer_round(value, sizeof(long) * CHAR_BIT);
}

long lroundf(float value) {
  return integer_round(value, sizeof(long) * CHAR_BIT);
}

long long llround(double value) {
  return integer_round(value, sizeof(long long) * CHAR_BIT);
}

long long llroundf(float value) {
  return integer_round(value, sizeof(long long) * CHAR_BIT);
}
