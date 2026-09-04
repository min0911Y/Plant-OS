#ifndef _CTYPES_H
#define _CTYPES_H
#define bool int
#define true 1
#define false 0
#ifndef NULL
#  ifdef __cplusplus
#   define NULL nullptr
#  else
#   define NULL ((void*)0)
#  endif
#endif
typedef __SIZE_TYPE__ size_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT8_TYPE__ int8_t;
typedef __INT16_TYPE__ int16_t;
typedef __INT32_TYPE__ int32_t;
typedef __INT64_TYPE__ int64_t;
#endif
