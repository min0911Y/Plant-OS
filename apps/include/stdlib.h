// Powerint DOS 386系统调用
// @ Copyright (C) 2022
// @ Author: zhouzhihao & min0911_
#ifndef STDLIB_H
#define STDLIB_H
#ifdef __cplusplus
extern "C" {
#endif
#include <ctypes.h>
#include <rand.h>
#define RAND_MAX 32767
void *malloc(size_t size);
void free(void *p);
void *realloc(void *ptr, uint32_t size);
void qsort (void *, size_t, size_t, int (*)(const void *, const void *));
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
float strtof(const char * nptr, char ** endptr);
long long strtoll(const char * nptr, char ** endptr, int base);
unsigned long long strtoull(const char * nptr, char ** endptr, int base);
double strtod(const char *nptr, char **endptr);
int atoi(const char * nptr);
int abs(int a);
double atof(const char *s);
void atexit(void (*func)(void));
void *calloc(size_t num, size_t size);
void abort(void);
void exit(unsigned status);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#ifdef __cplusplus
}
#endif
#endif
