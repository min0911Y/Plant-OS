#pragma once
#include <define.h>

void kprint(char *str);
void logk(char *str, ...);

#define Panic_Print(func, info, ...)                                                               \
  func("%s--PANIC: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__);
#define WARNING_Print(func, info, ...)                                                             \
  func("%s--WARNING: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__);
#define DEBUG_Print(func, info, ...)                                                               \
  func("%s--DEBUG: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__);
#define Panic_K(info, ...)   Panic_Print(logk, info, ##__VA_ARGS__)
#define WARNING_K(info, ...) WARNING_Print(logk, info, ##__VA_ARGS__)
#define DEBUG_K(info, ...)   DEBUG_Print(logk, info, ##__VA_ARGS__)
#define Panic_F(info, ...)   Panic_Print(printf, info, ##__VA_ARGS__)
#define WARNING_F(info, ...) WARNING_Print(printf, info, ##__VA_ARGS__)
#define DEBUG_F(info, ...)   DEBUG_Print(printf, info, ##__VA_ARGS__)

#undef _LOG
#define _LOG(type, fmt, ...)                                                                       \
  logk(CONCAT(STR, type) STR_LOGINFO CONCAT(COLOR, type) fmt CEND "\n", ARG_LOGINFO, ##__VA_ARGS__)

#define logk(fmt, ...) _LOG(_DEBUG, fmt, ##__VA_ARGS__)
