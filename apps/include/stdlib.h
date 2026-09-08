// Powerint DOS 386系统调用
// @ Copyright (C) 2022
// @ Author: zhouzhihao & min0911_
#ifndef STDLIB_H
#define STDLIB_H
#ifdef __cplusplus
extern "C" {
#endif
#include <ctypes.h>
#include <locale.h>
#include <rand.h>
#include <stddef.h>
#define RAND_MAX 32767
#define MB_CUR_MAX (__mb_cur_max())
int mblen(const char *text, size_t size);
int mbtowc(wchar_t *wide, const char *text, size_t size);
int wctomb(char *text, wchar_t wide);
typedef struct {
  int quot, rem;
} div_t;
typedef struct {
  long quot, rem;
} ldiv_t;
typedef struct {
  long long quot, rem;
} lldiv_t;
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);
long labs(long value);
long long llabs(long long value);
long atol(const char *text);
long long atoll(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
long double strtold(const char *text, char **end);
float strtof_l(const char *text, char **end, locale_t locale);
double strtod_l(const char *text, char **end, locale_t locale);
long double strtold_l(const char *text, char **end, locale_t locale);
long long strtoll_l(const char *text, char **end, int base, locale_t locale);
unsigned long long strtoull_l(const char *text, char **end, int base,
                              locale_t locale);
void *malloc(size_t size);
void free(void *p);
void *realloc(void *ptr, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **pointer, size_t alignment, size_t size);
void *memalign(size_t alignment, size_t size);
void qsort (void *, size_t, size_t, int (*)(const void *, const void *));
void qsort_r(void *base, size_t count, size_t size,
             int (*compare)(const void *, const void *, void *), void *context);
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
char *realpath(const char *path, char *resolved);
int mkstemp(char *path);
int mkstemps(char *path, int suffix_length);
float strtof(const char * nptr, char ** endptr);
long long strtoll(const char * nptr, char ** endptr, int base);
unsigned long long strtoull(const char * nptr, char ** endptr, int base);
double strtod(const char *nptr, char **endptr);
int atoi(const char * nptr);
int abs(int a);
double atof(const char *s);
int atexit(void (*func)(void));
void *calloc(size_t num, size_t size);
void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
void _exit(int status) __attribute__((noreturn));
void _Exit(int status) __attribute__((noreturn));
int at_quick_exit(void (*callback)(void));
void quick_exit(int status) __attribute__((noreturn));
void sleep(int time);
int system(char *command);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#ifdef __cplusplus
}
#endif
#endif
