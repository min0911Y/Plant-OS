#include "../third_party/musl/scan.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <rand.h>
#include <stdlib.h>
#include <string.h>

div_t div(int numerator, int denominator) {
  return (div_t){numerator / denominator, numerator % denominator};
}
ldiv_t ldiv(long numerator, long denominator) {
  return (ldiv_t){numerator / denominator, numerator % denominator};
}
lldiv_t lldiv(long long numerator, long long denominator) {
  return (lldiv_t){numerator / denominator, numerator % denominator};
}
long labs(long value) { return value < 0 ? -value : value; }
long long llabs(long long value) { return value < 0 ? -value : value; }
long atol(const char *text) { return strtol(text, NULL, 10); }
long long atoll(const char *text) { return strtoll(text, NULL, 10); }
typedef int (*sort_compare_t)(const void *, const void *);
static int sort_compare(const void *left, const void *right, void *context) {
  return (*(sort_compare_t *)context)(left, right);
}
void qsort(void *base, size_t count, size_t size, sort_compare_t compare) {
  qsort_r(base, count, size, sort_compare, &compare);
}
static long double parse_float(const char *text, char **end, int precision) {
  scan_input_t scan = {.string = text};
  long double value = plant_floatscan(&scan, precision, 1);
  if (end)
    *end = (char *)text + scan.count;
  return value;
}
float strtof(const char *text, char **end) { return parse_float(text, end, 0); }
double strtod(const char *text, char **end) {
  return parse_float(text, end, 1);
}
long double strtold(const char *text, char **end) {
  return parse_float(text, end, 2);
}

static unsigned long long parse_integer(const char *text, char **end, int base,
                                        unsigned long long limit) {
  scan_input_t scan = {.string = text};
  unsigned long long value = plant_intscan(&scan, base, 1, limit);
  if (end)
    *end = (char *)text + scan.count;
  return value;
}
unsigned long long strtoull(const char *text, char **end, int base) {
  return parse_integer(text, end, base, ULLONG_MAX);
}
long long strtoll(const char *text, char **end, int base) {
  return parse_integer(text, end, base, (unsigned long long)LLONG_MIN);
}
unsigned long strtoul(const char *text, char **end, int base) {
  return parse_integer(text, end, base, ULONG_MAX);
}
long strtol(const char *text, char **end, int base) {
  return parse_integer(text, end, base, 0UL + LONG_MIN);
}
intmax_t strtoimax(const char *text, char **end, int base) {
  return strtoll(text, end, base);
}
uintmax_t strtoumax(const char *text, char **end, int base) {
  return strtoull(text, end, base);
}
int mkstemps(char *path, int suffix_length) {
  size_t length = path ? strlen(path) : 0;
  if (suffix_length < 0 || (size_t)suffix_length > length ||
      length - (size_t)suffix_length < 6) {
    errno = EINVAL;
    return -1;
  }
  char *token = path + length - suffix_length - 6;
  if (memcmp(token, "XXXXXX", 6)) {
    errno = EINVAL;
    return -1;
  }
  static const char alphabet[] =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (;;) {
    uint64_t value = ((uint64_t)os_random32() << 32) | os_random32();
    for (unsigned i = 0; i < 6; i++) {
      token[i] = alphabet[value % (sizeof(alphabet) - 1)];
      value /= sizeof(alphabet) - 1;
    }
    int descriptor = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (descriptor >= 0 || errno != EEXIST)
      return descriptor;
  }
}

int mkstemp(char *path) { return mkstemps(path, 0); }
