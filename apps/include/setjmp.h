#ifndef _SET_JMP_H
#define _SET_JMP_H

#include <signal.h>

#define _NSETJMP 10

typedef long jmp_buf[_NSETJMP];
typedef struct __plant_sigjmp_context {
  jmp_buf context;
  sigset_t mask;
  int restore_mask;
} sigjmp_buf[1];

#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif

#define sigsetjmp(env, save_mask)                                            \
  (((env)[0].restore_mask = !!(save_mask)),                                  \
   ((env)[0].restore_mask                                                    \
        ? sigprocmask(SIG_SETMASK, (const sigset_t *)0, &(env)[0].mask)      \
        : 0),                                                               \
   setjmp((env)[0].context))

#endif
