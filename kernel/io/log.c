#include <dos.h>
#include <io.h>

void kprint(char *string) {
  for (; *string; string++)
    write_serial(*string);
}

void logk(char *format, ...) {
  char buffer[1024];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  kprint(buffer);
}
