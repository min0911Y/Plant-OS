#include <dos.h>
#include <io.h>

//系统日志打印

void kprint(char *str) {
  for (int i = 0; i < strlen(str); i++) {
    write_serial(str[i]);
  }
}

void logk(char *str, ...) {
  int     len;
  va_list ap;
  va_start(ap, str);
  char buf[1024];
  len = vsprintf(buf, str, ap);
  kprint(buf);
  va_end(ap);
}
