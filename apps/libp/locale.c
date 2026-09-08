#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <native_thread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>

enum { LOCALE_C = 1, LOCALE_UTF8 = 2 };
static unsigned global_locale = LOCALE_C;

static locale_t current_locale(void) {
  tls_control_t *tls = (void *)native_thread_pointer();
  return tls && tls->locale ? tls->locale
                            : __atomic_load_n(&global_locale, __ATOMIC_RELAXED);
}
locale_t newlocale(int mask, const char *name, locale_t base) {
  if ((mask & ~LC_ALL_MASK) || !name || base > LOCALE_UTF8) {
    errno = EINVAL;
    return 0;
  }
  locale_t selected;
  if (!strcmp(name, "C") || !strcmp(name, "POSIX") || !name[0])
    selected = LOCALE_C;
  else if (!strcmp(name, "C.UTF-8") || !strcmp(name, "C.utf8"))
    selected = LOCALE_UTF8;
  else {
    errno = ENOENT;
    return 0;
  }
  return mask & LC_CTYPE_MASK ? selected : base ? base : LOCALE_C;
}
locale_t duplocale(locale_t locale) {
  if (locale == LC_GLOBAL_LOCALE)
    return __atomic_load_n(&global_locale, __ATOMIC_RELAXED);
  if (locale == LOCALE_C || locale == LOCALE_UTF8)
    return locale;
  errno = EINVAL;
  return 0;
}
void freelocale(locale_t locale) {
  /* C and C.UTF-8 locales are immutable, interned values without allocations.
   */
}
locale_t uselocale(locale_t locale) {
  tls_control_t *tls = (void *)native_thread_pointer();
  locale_t previous = tls && tls->locale ? tls->locale : LC_GLOBAL_LOCALE;
  if (!locale)
    return previous;
  if (!tls || (locale != LOCALE_C && locale != LOCALE_UTF8 &&
               locale != LC_GLOBAL_LOCALE)) {
    errno = EINVAL;
    return 0;
  }
  tls->locale = locale == LC_GLOBAL_LOCALE ? 0 : locale;
  return previous;
}
char *setlocale(int category, const char *name) {
  if (category < LC_CTYPE || category > LC_ALL) {
    errno = EINVAL;
    return NULL;
  }
  locale_t selected = __atomic_load_n(&global_locale, __ATOMIC_RELAXED);
  if (name) {
    selected = newlocale(category == LC_ALL ? LC_ALL_MASK : 1 << category, name,
                         selected);
    if (!selected)
      return NULL;
    __atomic_store_n(&global_locale, selected, __ATOMIC_RELAXED);
  }
  return (category == LC_ALL || category == LC_CTYPE) && selected == LOCALE_UTF8
             ? "C.UTF-8"
             : "C";
}
size_t __mb_cur_max(void) { return current_locale() == LOCALE_UTF8 ? 4 : 1; }

/* Both supported locales use ASCII byte classification and C numeric syntax. */
#define LOCALE_CTYPE(name)                                                     \
  int name##_l(int c, locale_t locale) { return name(c); }
LOCALE_CTYPE(isalnum)
LOCALE_CTYPE(isalpha)
LOCALE_CTYPE(isblank)
LOCALE_CTYPE(iscntrl)
LOCALE_CTYPE(isdigit)
LOCALE_CTYPE(isgraph)
LOCALE_CTYPE(islower)
LOCALE_CTYPE(isprint)
LOCALE_CTYPE(isspace)
LOCALE_CTYPE(ispunct)
LOCALE_CTYPE(isupper)
LOCALE_CTYPE(isxdigit)
LOCALE_CTYPE(tolower)
LOCALE_CTYPE(toupper)
#undef LOCALE_CTYPE
float strtof_l(const char *text, char **end, locale_t locale) {
  return strtof(text, end);
}
double strtod_l(const char *text, char **end, locale_t locale) {
  return strtod(text, end);
}
long double strtold_l(const char *text, char **end, locale_t locale) {
  return strtold(text, end);
}
long long strtoll_l(const char *text, char **end, int base, locale_t locale) {
  return strtoll(text, end, base);
}
unsigned long long strtoull_l(const char *text, char **end, int base,
                              locale_t locale) {
  return strtoull(text, end, base);
}
int strcoll_l(const char *left, const char *right, locale_t locale) {
  return strcmp(left, right);
}
size_t strxfrm(char *destination, const char *source, size_t size) {
  size_t length = strlen(source);
  if (size)
    memcpy(destination, source, length < size ? length + 1 : size);
  return length;
}
size_t strxfrm_l(char *destination, const char *source, size_t size,
                 locale_t locale) {
  return strxfrm(destination, source, size);
}
size_t strftime_l(char *s, size_t size, const char *format,
                  const struct tm *time, locale_t locale) {
  return strftime(s, size, format, time);
}
