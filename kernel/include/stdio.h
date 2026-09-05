#ifndef _STDIO_H
#define _STDIO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdarg.h>
#include <stddef.h>
int printk(const char* format, ...);
int sprintf(char* s, const char* format, ...);
int vsprintf(char* s, const char* format, va_list arg);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif
