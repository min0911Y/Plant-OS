#include <mouse.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <rand.h>
#include <time.h>
#include <limits.h>
typedef unsigned int uintmax_t;
typedef uintmax_t uintptr_t;
typedef int intmax_t;

#define SZ_4K 0x1000
int GetMouse_x(int mouse)
{
    unsigned short high = ((short *)(&mouse))[1];
    //获取mouse的高十六位
    high = mouse >> 16;
    //获取mouse的低十六位
    int low = mouse & 0xFFFF;

    char x = (char)((high & 0xFF));
    char y = ((char *)(&high))[1];
    char btn = low & 0xf;
    return x;
}
int GetMouse_y(int mouse)
{
    unsigned short high = ((short *)(&mouse))[1];
    //获取mouse的高十六位
    high = mouse >> 16;
    //获取mouse的低十六位
    int low = mouse & 0xFFFF;

    char x = (char)((high & 0xFF));
    char y = ((char *)(&high))[1];
    char btn = low & 0xf;
    return y;
}
int GetMouse_btn(int mouse)
{
    unsigned short high = ((short *)(&mouse))[1];
    //获取mouse的高十六位
    high = mouse >> 16;
    //获取mouse的低十六位
    int low = mouse & 0xFFFF;

    char x = (char)((high & 0xFF));
    char y = ((char *)(&high))[1];
    char btn = low & 0xf;
    return btn;
}
