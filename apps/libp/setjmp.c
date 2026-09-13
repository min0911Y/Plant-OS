#include <setjmp.h>

void siglongjmp(sigjmp_buf env, int value) {
  if (env[0].restore_mask)
    sigprocmask(SIG_SETMASK, &env[0].mask, (sigset_t *)0);
  longjmp(env[0].context, value);
}
