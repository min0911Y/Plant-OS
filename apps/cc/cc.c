// 利用HZK字库编码制成的汉字拼音输入法
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <syscall.h>

static char pinyin[100] = {0};
int main(int argc, char **argv) {
  struct stat status;
  if (stat("hanzi.txt", &status) != 0 || status.st_size == 0) {
    print("Cannot find hanzi.txt.");
    return 0;
  }
  unsigned char *hanzi = (unsigned char *)malloc(status.st_size);
  FILE *stream = fopen("hanzi.txt", "rb");
  if (hanzi == NULL || stream == NULL ||
      fread(hanzi, 1, status.st_size, stream) != status.st_size) {
    return 1;
  }
  fclose(stream);
  printf("请输入拼音：");
  scan(pinyin, 100);
  printf(pinyin);
  printf("\n");
  char *p = hanzi;
  char *q = hanzi;
  while (*p != '\0') {
    if (*p == '\n') {
      *p = '\0';
      // 找到拼音
      char *bmp = q;
      // 找到空格
      while (*bmp != ' ')
        bmp++;
      bmp++;
      if (strncmp(pinyin, bmp, strlen(pinyin)) == 0) {
        printf(q);
        printf(" ");
      }
      q = p + 1;
    }
    p++;
  }
  printf("\n请按ENTER返回系统。。。");
  while (getch() != 0x0a)
    ;
  free(hanzi);
  return 0;
}
