#include <dos.h>
#include <ctype.h>
#include <irq.h>
#include <stdio.h>
#include <string.h>
void UInt2BinAscii(unsigned int num, char* buf);
#define LONG_MAX 0x7fffffff
#define LONG_MIN (-LONG_MAX - 1)

// strcmp
int strcmp(const char* s1, const char* s2) {
  while (*s1 == *s2) {
    if (*s1 == '\0')
      return 0;
    s1++;
    s2++;
  }
  return *s1 - *s2;
}
// strcpy
char* strcpy(char* dest, const char* src) {
  char* tmp = dest;
  while ((*dest++ = *src++) != '\0')
    ;
  return tmp;
}
// strncpy
char* strncpy(char* dest, const char* src, size_t n) {
  char* tmp = dest;
  while (n-- > 0 && (*dest++ = *src++) != '\0')
    ;
  return tmp;
}
// strlen
size_t strlen(const char* s) {
  size_t len = 0;
  while (s[len])
    len++;
  return len;
}
// strcat
char* strcat(char* dest, const char* src) {
  char* tmp = dest;
  while (*dest)
    dest++;
  while ((*dest++ = *src++) != '\0')
    ;
  return tmp;
}
// strncat
char* strncat(char* dest, const char* src, size_t n) {
  char* tmp = dest;
  while (*dest)
    dest++;
  while (n-- > 0 && (*dest++ = *src++) != '\0')
    ;
  return tmp;
}
// memset
void* memset(void* s, int c, size_t n) {
  typedef uintptr_t alias_word_t __attribute__((may_alias));
  unsigned char *destination = s;
  unsigned char byte = (unsigned char)c;
  alias_word_t pattern = byte;
  for (unsigned shift = 8; shift < sizeof(pattern) * 8; shift *= 2) {
    pattern |= pattern << shift;
  }
  while (n != 0 && ((uintptr_t)destination & (sizeof(pattern) - 1)) != 0) {
    *destination++ = byte;
    n--;
  }
  alias_word_t *wide = (alias_word_t *)destination;
  while (n >= sizeof(pattern)) {
    *wide++ = pattern;
    n -= sizeof(pattern);
  }
  destination = (unsigned char *)wide;
  while (n-- != 0) {
    *destination++ = byte;
  }
  return s;
}
// strtol
long strtol(const char* nptr, char** endptr, int base) {
  long acc = 0;
  int c;
  unsigned long cutoff;
  int neg = 0, any, cutlim;

  /*
   * Skip white space and pick up leading +/- sign if any.
   * If base is 0, allow 0x for hex and 0 for octal, else
   * assume decimal; if base is already 16, allow 0x.
   */
  do {
    c = *nptr++;
  } while (isspace(c));
  if (c == '-') {
    neg = 1;
    c = *nptr++;
  } else if (c == '+')
    c = *nptr++;
  if ((base == 0 || base == 16) && c == '0' && (*nptr == 'x' || *nptr == 'X')) {
    c = nptr[1];
    nptr += 2;
    base = 16;
  } else if ((base == 0 || base == 2) && c == '0' &&
             (*nptr == 'b' || *nptr == 'B')) {
    c = nptr[1];
    nptr += 2;
    base = 2;
  } else if (base == 0)
    base = c == '0' ? 8 : 10;

  /*
   * Compute the cutoff value between legal numbers and illegal
   * numbers.  That is the largest legal value, divided by the
   * base.  An input number that is greater than this value, if
   * followed by a legal input character, is too big.  One that
   * is equal to this value may be valid or not; the limit
   * between valid and invalid numbers is then based on the last
   * digit.  For instance, if the range for longs is
   * [-2147483648..2147483647] and the input base is 10,
   * cutoff will be set to 214748364 and cutlim to either
   * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
   * a value > 214748364, or equal but the next digit is > 7 (or 8),
   * the number is too big, and we will return a range error.
   */
  cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
  cutlim = cutoff % (unsigned long)base;
  cutoff /= (unsigned long)base;
  for (acc = 0, any = 0;; c = *nptr++) {
    if (isdigit(c))
      c -= '0';
    else if (isalpha(c))
      c -= isupper(c) ? 'A' - 10 : 'a' - 10;
    else
      break;
    if (c >= base)
      break;
    if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
      any = -1;
    else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if (any < 0) {
    acc = neg ? LONG_MIN : LONG_MAX;
    // errno = ERANGE;
    print("panic: strtol: overflow\n");
  } else if (neg)
    acc = -acc;
  if (endptr != 0)
    *endptr = (char*)(any ? nptr : (char*)nptr - 1);
  return (acc);
}
// memcmp
int memcmp(const void* s1, const void* s2, size_t n) {
  const unsigned char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2)
      return *p1 - *p2;
    p1++, p2++;
  }
  return 0;
}
// memcpy
void* memcpy(void* s, const void* ct, size_t n) {
  typedef uintptr_t alias_word_t __attribute__((may_alias));
  unsigned char *destination = s;
  const unsigned char *source = ct;
  if ((((uintptr_t)destination | (uintptr_t)source) &
       (sizeof(alias_word_t) - 1)) == 0) {
    alias_word_t *wide_destination = (alias_word_t *)destination;
    const alias_word_t *wide_source = (const alias_word_t *)source;
    while (n >= sizeof(alias_word_t)) {
      *wide_destination++ = *wide_source++;
      n -= sizeof(alias_word_t);
    }
    destination = (unsigned char *)wide_destination;
    source = (const unsigned char *)wide_source;
  }
  while (n-- != 0) {
    *destination++ = *source++;
  }
  return s;
}
// isspace
int isspace(int c) {
  return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
          c == '\v');
}
// isdigit
int isdigit(int c) {
  return (c >= '0' && c <= '9');
}
// isalpha
int isalpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
// isupper
int isupper(int c) {
  return (c >= 'A' && c <= 'Z');
}
// strncmp
int strncmp(const char* s1, const char* s2, size_t n) {
  const unsigned char *p1 = (const unsigned char*)s1,
                      *p2 = (const unsigned char*)s2;
  while (n-- > 0) {
    if (*p1 != *p2)
      return *p1 - *p2;
    if (*p1 == '\0')
      return 0;
    p1++, p2++;
  }
  return 0;
}
#define FLOAT_PRINT_PRECISION 6

const char _plos_lut_alnum_lower[] = "0123456789abcdefghijklmnopqrstuvwxyz";
const char _plos_lut_alnum_upper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static char* format_float_common(char* buf, int buf_size, long double value) {
  char int_buf[16];
  char* p = buf;
  int int_len = 0;
  int negative = 0;
  int precision = FLOAT_PRINT_PRECISION;
  uint32_t int_part = 0;
  long double frac_part = 0.0L;
  long double rounding = 0.5L;

  if (buf == NULL || buf_size <= 0) {
    return buf;
  }

  buf[0] = '\0';

  if (value != value) {
    strncpy(buf, "nan", buf_size - 1);
    buf[buf_size - 1] = '\0';
    return buf;
  }

  if (value < 0) {
    negative = 1;
    value = -value;
  }

  for (int i = 0; i < precision; i++) {
    rounding /= 10.0L;
  }
  value += rounding;

  if (value > 4294967295.0L) {
    const char* text = negative ? "-inf" : "inf";
    strncpy(buf, text, buf_size - 1);
    buf[buf_size - 1] = '\0';
    return buf;
  }

  int_part = (uint32_t)value;
  frac_part = value - (long double)int_part;

  if (negative && p - buf < buf_size - 1) {
    *p++ = '-';
  }

  do {
    int_buf[int_len++] = '0' + (int)(int_part % 10);
    int_part /= 10;
  } while (int_part && int_len < (int)sizeof(int_buf));

  while (int_len > 0 && p - buf < buf_size - 1) {
    *p++ = int_buf[--int_len];
  }

  if (precision > 0 && p - buf < buf_size - 1) {
    *p++ = '.';
    for (int i = 0; i < precision && p - buf < buf_size - 1; i++) {
      int digit = 0;

      frac_part *= 10.0L;
      if (frac_part < 0) {
        frac_part = 0;
      }
      digit = (int)frac_part;
      if (digit < 0) {
        digit = 0;
      } else if (digit > 9) {
        digit = 9;
      }
      *p++ = '0' + digit;
      frac_part -= digit;
    }

    while (p > buf && p[-1] == '0') {
      p--;
    }
    if (p > buf && p[-1] == '.') {
      p--;
    }
  }

  *p = '\0';
  return buf;
}

char* fftostr(char* buf, int buf_size, float value) {
  return format_float_common(buf, buf_size, (long double)value);
}

char* ftostr(char* buf, int buf_size, double value) {
  return format_float_common(buf, buf_size, (long double)value);
}

char* fltostr(char* buf, int buf_size, long double value) {
  return format_float_common(buf, buf_size, value);
}

void strrev(char* s) {
  if (NULL == s)
    return;

  char* pBegin = s;
  char* pEnd = s + strlen(s) - 1;

  char pTemp;

  while (pBegin < pEnd) {
    pTemp = *pBegin;
    *pBegin = *pEnd;
    *pEnd = pTemp;

    ++pBegin, --pEnd;
  }
}
int printk(const char* format, ...) {
  int len;
  va_list ap;
  va_start(ap, format);
  char buf[1024];
  len = vsnprintf(buf, sizeof(buf), format, ap);
  print(buf);
  va_end(ap);
  return len;
}
void F2S(double d, char* str, int l) {
  int n = (int)d;  //去掉小数点
  int b = 0;
  int i, j;
  double m = d;  //
  char* buf;
  buf = malloc(128);
  sprintf(str, "%d", n);
  i = strlen(str);
  str[i] = '.';  //小数点
  str[i + 1] = 0;
  while (d > 1.0) {
    d /= 10.0;
    b++;
  }
  for (i = 0; i < l; i++) {
    m *= 10;  //扩大
  }
  n = (int)m;  //放弃其他小数点
  sprintf(buf, "%d", n);
  for (i = b, j = strlen(str); i < strlen(buf); i++, j++) {
    str[j] = buf[i];
  }
  str[j] = 0;
  free(buf);
  // 5.55 => 0.555 => <1 = true and b = 1
  // 1.56 0.156,15.6,156
}
char* strchr(const char* s, int c) {
  char* p = s;
  while (*p && *p != c) {
    p++;
  }
  if (*p == c) {
    return p;
  }
  return NULL;
}
char* strrchr(const char* s1, int ch) {
  char* s2;
  char* s3;
  s2 = strchr(s1, ch);
  while (s2 != NULL) {
    s3 = strchr(s2 + 1, ch);
    if (s3 != NULL) {
      s2 = s3;
    } else {
      return s2;
    }
  }
  return NULL;
}
void* memmove(void* dest, const void* src, int n) {
  if (n <= 0) {
    return dest;
  }
  assert(dest != NULL);
  assert(src != NULL);
  if (dest == src) {
    return dest;
  }
  char* pdest = (char*)dest;
  const char* psrc = (const char*)src;
  uintptr_t destination_address = (uintptr_t)dest;
  uintptr_t source_address = (uintptr_t)src;
  if (destination_address < source_address) {
    for (int i = 0; i < n; i++) {
      pdest[i] = psrc[i];
    }
  } else {
    for (int i = n; i != 0; i--) {
      pdest[i - 1] = psrc[i - 1];
    }
  }
  return dest;
}
void abort(void) {
  logk("abort() called\n");
  (void)irq_save();
  for (;;) {
  }
}
void assert(int expression) {
  if (!expression) {
    logk("AN ERROR\n");
  }
}
void* memchr(const void* s, int c, size_t n) {
  unsigned char* p = (unsigned char*)s;
  for (; n-- > 0; ++p) {
    if (*p == c)
      return (void*)p;
  }
  return NULL;
}
int stricmp(const char* s1, const char* s2) {
  char* rs1 = malloc(strlen(s1) + 1);
  char* rs2 = malloc(strlen(s2) + 1);
  strcpy(rs1, s1);
  strcpy(rs2, s2);
  strtoupper(rs1);
  strtoupper(rs2);
  int result = strcmp(rs1, rs2);
  free(rs1);
  free(rs2);
  return result;
}
int strincmp(const char* s1, const char* s2, size_t n) {
  char* rs1 = malloc(strlen(s1) + 1);
  char* rs2 = malloc(strlen(s2) + 1);
  strcpy(rs1, s1);
  strcpy(rs2, s2);
  strtoupper(rs1);
  strtoupper(rs2);
  int result = strncmp(rs1, rs2, n);
  free(rs1);
  free(rs2);
  return result;
}
unsigned long int strtoul(const char* nptr, char** endptr, int base) {
  unsigned long int result = 0;
  while (*nptr != '\0') {
    if (*nptr >= '0' && *nptr <= '9') {
      result = result * base + (*nptr - '0');
    } else if (*nptr >= 'a' && *nptr <= 'z') {
      result = result * base + (*nptr - 'a' + 10);
    } else if (*nptr >= 'A' && *nptr <= 'Z') {
      result = result * base + (*nptr - 'A' + 10);
    } else {
      break;
    }
    nptr++;
  }
  if (endptr != NULL) {
    *endptr = (char*)nptr;
  }
  return result;
}
