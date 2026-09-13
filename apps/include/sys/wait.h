#ifndef PLANT_SYS_WAIT_H
#define PLANT_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#define WEXITED 4
#define WNOWAIT 0x01000000

typedef enum {
  P_ALL = 0,
  P_PID = 1,
  P_PGID = 2,
} idtype_t;

#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WTERMSIG(status) ((status)&0x7f)
#define WSTOPSIG(status) WEXITSTATUS(status)
#define WIFEXITED(status) (WTERMSIG(status) == 0)
#define WIFSIGNALED(status)                                                   \
  (((signed char)(((status)&0x7f) + 1) >> 1) > 0)
#define WIFSTOPPED(status) (((status)&0xff) == 0x7f)
#define WIFCONTINUED(status) ((status) == 0xffff)

#ifdef __cplusplus
extern "C" {
#endif
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
int waitid(idtype_t idtype, id_t id, siginfo_t *information, int options);
#ifdef __cplusplus
}
#endif

#endif
