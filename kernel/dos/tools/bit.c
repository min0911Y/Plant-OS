/**
 * @file bit.c
 * @author min0911_
 * @brief  实现位操作的函数
 * @version 0.1
 * @date 2022-07-19
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <dos.h>
static inline int Switch8(int x) {
  return 7 - x;
}
static inline int Switch16(int x) {
  return 15 - x;
}
static inline int Switch32(int x) {
  return 31 - x;
}
static inline int Switch64(int x) {
  return 63 - x;
}
typedef _Bool Bit; // 定义一个位类型
void          WriteBitMem8(u8 *mem, Bit bit, int n) {
  n = Switch8(n);
  // 将第n个位置为bit的值写入到mem中
  if (bit) {
    *mem |= (1 << n);
  } else {
    *mem &= ~(1 << n);
  }
}
Bit ReadBitMem8(u8 *mem, int n) {
  n = Switch8(n);
  // 读取第n个位置的值
  return *mem & (1 << n);
}
void WriteBitMem16(u16 *mem, Bit bit, int n) {
  n = Switch16(n);
  // 将第n个位置为bit的值写入到mem中
  if (bit) {
    *mem |= (1 << n);
  } else {
    *mem &= ~(1 << n);
  }
}
Bit ReadBitMem16(u16 *mem, int n) {
  n = Switch16(n);
  // 读取第n个位置的值
  return *mem & (1 << n);
}
void WriteBitMem32(u32 *mem, Bit bit, int n) {
  n = Switch32(n);
  // 将第n个位置为bit的值写入到mem中
  if (bit) {
    *mem |= (1 << n);
  } else {
    *mem &= ~(1 << n);
  }
}
Bit ReadBitMem32(u32 *mem, int n) {
  n = Switch32(n);
  // 读取第n个位置的值
  return *mem & (1 << n);
}
void WriteBitMem64(u64 *mem, Bit bit, int n) {
  n = Switch64(n);
  // 将第n个位置为bit的值写入到mem中
  if (bit) {
    *mem |= (1 << n);
  } else {
    *mem &= ~(1 << n);
  }
}
Bit ReadBitMem64(u64 *mem, int n) {
  n = Switch64(n);
  // 读取第n个位置的值
  return *mem & (1 << n);
}
int GetNoZeroBitIndex(u32 x) {
  for (int i = 0; i < 32; i++)
    if (ReadBitMem32(&x, i)) return i;
  return -1; /* 应该不会到这里。。。 */
}
void UInt2BinAscii(u32 num, char *buf) {
  // 将整数num转换为二进制字符串并写入buf中
  int i = 0;
  int n = 0;
  for (i = GetNoZeroBitIndex(num), n = 0; i < 32; i++) {
    buf[n++] = ReadBitMem32(&num, i) ? '1' : '0';
  }
  buf[32] = '\0';
  // strrev(buf);
}