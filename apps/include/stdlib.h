// Powerint DOS 386系统调用
// @ Copyright (C) 2022
// @ Author: zhouzhihao & min0911_
#ifndef STDLIB_H
#define STDLIB_H
#ifdef __cplusplus
extern "C" {
#endif
#include <ctypes.h>
#include <syscall.h>
#define RAND_MAX 32767
void putch(char ch);
unsigned int getch();
void *malloc(size_t size);
void free(void *p);
void *realloc(void *ptr, uint32_t size);
void qsort (void *, size_t, size_t, int (*)(const void *, const void *));
char *getenv(char *name);
float strtof(const char * nptr, char ** endptr);
long long strtoll(const char * nptr, char ** endptr, int base);
unsigned long long strtoull(const char * nptr, char ** endptr, int base);
double strtod(const char *nptr, char **endptr);
int atoi(const char * nptr);
int abs(int a);
double atof(const char *s);
void atexit(void (*func)(void));
void *calloc(int num, size_t size) ;
void abort(void);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#ifdef __cplusplus
}
#endif
#endif
