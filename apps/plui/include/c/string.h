#pragma once
#include <define.h>
#include <type.h>

#if NO_STD

// char tolower(char c);
// char toupper(char c);

#  if __has(cbrt)
#  else
#  endif

#  if __has(memcpy)
finline void *memcpy(void *_rest _d, const void *_rest _s, size_t _n) {
  return __builtin_memcpy(_d, _s, _n);
}
#  else
dlimport void *memcpy(void *_rest _d, const void *_rest _s, size_t _n);
#  endif

#  if __has(memmove)
finline void *memmove(void *_d, const void *_s, size_t _n) {
  return __builtin_memmove(_d, _s, _n);
}
#  else
dlimport void *memmove(void *_d, const void *_s, size_t _n);
#  endif

#  if __has(memset)
finline void *memset(void *_s, int _c, size_t _n) {
  return __builtin_memset(_s, _c, _n);
}
#  else
dlimport void *memset(void *_s, int _c, size_t _n);
#  endif

dlimport int memcmp(const void *_s1, const void *_s2, size_t _n);

dlimport void *memchr(const void *_s, int _c, size_t _n);

dlimport char *strcpy(char *_rest d, cstr _rest s);

dlimport char *strncpy(char *_rest d, cstr _rest s, size_t n);

dlimport char *strcat(char *_rest _d, cstr _rest _s);

dlimport char *strncat(char *_rest __dest, cstr _rest __src, size_t __n);

dlimport int strcmp(cstr _s1, cstr _s2);

void *memccpy(void *_rest _d, const void *_rest _s, int _c, size_t _n);

void *rawmemchr(const void *_s, int _c);

void *memrchr(const void *_s, int _c, size_t _n);

int strcmp_ci(cstr _s1, cstr _s2); // case-insensitive

int strncmp(cstr _s1, cstr _s2, size_t n);

extern char *strdup(cstr __s);

extern char *strndup(cstr __string, size_t __n);

extern char *strchr(cstr __s, int __c);

extern char *strrchr(cstr __s, int __c);

extern char *strchrnul(cstr __s, int __c);

extern size_t strcspn(cstr __s, cstr __reject);

size_t strspn(cstr s, cstr accept);

extern char *strpbrk(cstr __s, cstr __accept);

extern char *strstr(cstr __haystack, cstr __needle);

extern char *strtok(char *_rest __s, cstr _rest __delim);

extern char *strtok_r(char *_rest __s, cstr _rest __delim, char **_rest __save_ptr);

extern char *strcasestr(cstr __haystack, cstr __needle);

extern void *memmem(const void *__haystack, size_t __haystacklen, const void *__needle,
                    size_t __needlelen);

extern void *mempcpy(void *_rest __dest, const void *_rest __src, size_t __n);

extern size_t strlen(cstr __s);

extern size_t strnlen(cstr __string, size_t __maxlen);

char *strerror(int e);

char *strerror_r(int e, char *buf, size_t n);

extern cstr strerrordesc_np(int __err);

extern cstr strerrorname_np(int __err);

extern void explicit_bzero(void *__s, size_t __n);

extern char *strsep(char **_rest __stringp, cstr _rest __delim);

extern char *strsignal(int __sig);

extern cstr sigabbrev_np(int __sig);

extern cstr sigdescr_np(int __sig);

extern char *stpcpy(char *_rest __dest, cstr _rest __src);

extern char *stpncpy(char *_rest __dest, cstr _rest __src, size_t __n);

extern int strverscmp(cstr __s1, cstr __s2);

extern char *strfry(char *__string);

extern void *memfrob(void *__s, size_t __n);

extern char *basename(cstr __filename);

#endif
