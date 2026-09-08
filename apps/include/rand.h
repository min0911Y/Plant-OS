/*随机数函数存放*/
/*Copyright (C) 2022 min0911_*/
#ifndef RAND_H
#define RAND_H
#include <ctypes.h>
#ifdef __cplusplus
extern "C" {
#endif
void srand(unsigned seed); //设置随机数种子
int rand(void);            //获取随机数
int RAND(void);              //使用时钟获取的随机数（可作为随机数种子使用）
uint32_t os_random32(void);

#ifdef __cplusplus
}
#endif
#endif
