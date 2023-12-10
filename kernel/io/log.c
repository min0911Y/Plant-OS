#include <io.h>
#include <dos.h>
//系统日志打印
void kprint(char *str) {
  //irq_mask_set(0);
  for (int i = 0; i < strlen(str); i++) {
    write_serial(str[i]);
  }
  //irq_mask_clear(0);
}
void logk(char *str, ...) {
  int len;
  va_list ap;
  va_start(ap, str);
  char *buf = page_malloc(1024);
  len = vsprintf(buf, str,ap);
  kprint(buf);
  va_end(ap);
  page_free(buf,1024);
}
