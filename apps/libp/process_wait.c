#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <syscall.h>

pid_t waitpid(pid_t pid, int *status, int options) {
  if (pid <= 0 || options) {
    errno = ENOTSUP;
    return -1;
  }

  int exit_status = waittid((unsigned)pid);
  if (exit_status < 0) {
    errno = ECHILD;
    return -1;
  }
  if (status)
    *status = (exit_status & 0xff) << 8;
  return pid;
}

pid_t wait(int *status) { return waitpid(-1, status, 0); }

int waitid(idtype_t idtype, id_t id, siginfo_t *information, int options) {
  if (idtype != P_PID || !information || !(options & WEXITED) ||
      (options & ~(WEXITED | WNOWAIT))) {
    errno = EINVAL;
    return -1;
  }

  int status;
  if (waitpid((pid_t)id, &status, 0) < 0)
    return -1;
  memset(information, 0, sizeof(*information));
  information->si_signo = SIGCHLD;
  if (WIFEXITED(status)) {
    information->si_code = CLD_EXITED;
    information->si_status = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    information->si_code = CLD_KILLED;
    information->si_status = WTERMSIG(status);
  } else {
    information->si_code = CLD_TRAPPED;
    information->si_status = status;
  }
  return 0;
}
