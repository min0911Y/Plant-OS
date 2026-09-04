#ifndef _STDARG_H
#define _STDARG_H
typedef __builtin_va_list va_list;
#define va_start(ap, value) __builtin_va_start((ap), (value))
#define va_arg(ap, type) __builtin_va_arg((ap), type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(destination, source)                                      \
  __builtin_va_copy((destination), (source))
#endif
