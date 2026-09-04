// 杂项函数
// Copyright (C) 2021-2022 zhouzhihao & min0911_
// ------------------------------------------------
#include <dos.h>

void insert_char(char *str, int pos, char ch) {
  int i;
  for (i = strlen(str); i >= pos; i--) {
    str[i + 1] = str[i];
  }
  str[pos] = ch;
}
void delete_char(char *str, int pos) {
  int i;
  for (i = pos; i < strlen(str); i++) {
    str[i] = str[i + 1];
  }
}
char bcd2hex(char bcd) {
  char i;
  if (bcd > 0x10 || bcd == 0x10) {
    i = (bcd / 0x10) * 6;
    i = i + bcd - (bcd / 0x10) * 0x10;
    return i;
  } else {
    return bcd;
  }
}
char hex2bcd(char hex) {
  char i;
  if (hex > 0x0a || hex == 0x0a) {
    i = (hex / 0x0a) * 0x10;
    i = i + hex - (hex / 0x0a) * 0x0a;
    return i;
  } else {
    return hex;
  }
}
char ascii2num(char c) {
  if (c > 'A' - 1 && c < 'F' + 1) {
    c = c - 0x37;
  } else if (c > 'a' - 1 && c < 'f' + 1) {
    c = c - 0x57;
  } else if (c > '0' - 1 && c < '9' + 1) {
    c = c - 0x30;
  }
  return c;
}
char num2ascii(char c) {
  if (c > 0x9 && c < 0x10) {
    c = c + 0x37;
  } else if (c < 0x0a) {
    c = c + 0x30;
  }
  return c;
}
void strtoupper(char *str) {
  while (*str != '\0') {
    if (*str >= 'a' && *str <= 'z') {
      *str -= 32;
    }
    str++;
  }
}
int GetCHorEN(unsigned char *str) {
  // 获取这个字符是中文全角还是英文半角
  if (str[0] > 0x80 && str[1] > 0x80) {
    return 1;
  } else if (str[0] > 0x80 && str[1] < 0x80) {
    return 0;
  } else {
    return 0;
  }
}
void clean(char *s, int len) {
  // 清理某个内存区域（全部置0）
  int i;
  for (i = 0; i != len; i++) {
    s[i] = 0;
  }
  return;
}
