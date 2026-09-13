#ifndef PLANT_UTMPX_H
#define PLANT_UTMPX_H

#include <sys/time.h>

#define _UTX_LINESIZE 32

struct utmpx {
  short ut_type;
  char ut_line[_UTX_LINESIZE];
  struct timeval ut_tv;
};

#ifdef __cplusplus
extern "C" {
#endif
void setutxent(void);
struct utmpx *getutxent(void);
void endutxent(void);
#ifdef __cplusplus
}
#endif

#endif
