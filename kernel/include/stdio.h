#pragma once
#include <define.h>
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif
int printk(const char *format, ...);
int sprintf(char *s, const char *format, ...);
int vsprintf(char *s, const char *format, va_list arg);
int vsnprintf(char *str, u32 size, const char *format, va_list ap);
int snprintf(char *str, u32 size, const char *format, ...);
#ifdef __cplusplus
}
#endif
