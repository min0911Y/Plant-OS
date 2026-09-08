// string.h
// By min0911_
#ifndef STRING_H_
#define STRING_H_
#ifdef __cplusplus
extern "C" {
#endif
#include <ctypes.h>
#include <locale.h>
char *strerror(int error_number);
int strerror_r(int error_number, char *buffer, size_t size);
char *strtok(char *text, const char *separators);
int strcmp(const char* s1, const char* s2);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t length);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
size_t strlcpy(char *destination, const char *source, size_t capacity);
size_t strlen(const char* s);
size_t strnlen(const char *s, size_t maximum);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
int memcmp(const void *vl, const void *vr, size_t n);
int strncmp(const char* s1, const char* s2, size_t n);
void strrev(char* s);
void F2S(double d, char* str, int l);
char *strchr(const char *s, int c);
char *strrchr(const char *s1, int ch);
char *strpbrk(const char *s, const char *accept);
char *strstr(const char *p1, const char *p2);
int strcoll(const char *str1, const char *str2);
size_t strxfrm(char *destination, const char *source, size_t size);
int strcoll_l(const char *left, const char *right, locale_t locale);
size_t strxfrm_l(char *destination, const char *source, size_t size,
                 locale_t locale);
double strtod(const char * nptr, char ** endptr);
size_t strcspn(const char *s, const char *c);
size_t strspn(const char* s, const char *c);
void* memchr(const void* s, int c, size_t n);
void *memset(void *dest, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *mempcpy(void *dest, const void *src, size_t n);
void *memmove(void *_d, const void *_s, size_t _n);
char *strdup(const char *s);
char *strndup(const char *s, size_t limit);
char *strsep(char **string, const char *separators);
#ifdef __cplusplus
}
#endif
#endif
