#ifndef _XLIBC_WCHAR_H
#define _XLIBC_WCHAR_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned int wint_t;
typedef struct {
  uint32_t value, state;
} mbstate_t;
#define WEOF ((wint_t)-1)
#ifdef __cplusplus
extern "C" {
#endif
size_t mbrtowc(wchar_t *wide, const char *text, size_t size, mbstate_t *state);
size_t mbrlen(const char *text, size_t size, mbstate_t *state);
size_t wcrtomb(char *text, wchar_t wide, mbstate_t *state);
int mbsinit(const mbstate_t *state);
#ifdef __cplusplus
}
#endif
#endif  /* _XLIBC_WCHAR_H */
