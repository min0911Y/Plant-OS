#include <errno.h>
#include <sys/resource.h>

int getloadavg(double averages[], int count) {
  if (!averages || count < 1 || count > 3) {
    errno = EINVAL;
    return -1;
  }
  errno = ENOTSUP;
  return -1;
}

int getrlimit(int resource, struct rlimit *limits) {
  if (!limits) {
    errno = EFAULT;
    return -1;
  }

  switch (resource) {
  case RLIMIT_CORE:
    limits->rlim_cur = 0;
    limits->rlim_max = 0;
    return 0;
  case RLIMIT_CPU:
  case RLIMIT_FSIZE:
  case RLIMIT_DATA:
  case RLIMIT_STACK:
  case RLIMIT_RSS:
  case RLIMIT_NPROC:
  case RLIMIT_NOFILE:
  case RLIMIT_MEMLOCK:
  case RLIMIT_AS:
    limits->rlim_cur = RLIM_INFINITY;
    limits->rlim_max = RLIM_INFINITY;
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

int setrlimit(int resource, const struct rlimit *limits) {
  if (!limits) {
    errno = EFAULT;
    return -1;
  }
  if (resource < 0 || resource >= RLIMIT_NLIMITS) {
    errno = EINVAL;
    return -1;
  }
  errno = ENOTSUP;
  return -1;
}

int getpriority(int which, id_t who) {
  if (which < PRIO_PROCESS || which > PRIO_USER) {
    errno = EINVAL;
    return -1;
  }
  errno = ENOTSUP;
  return -1;
}

int setpriority(int which, id_t who, int priority) {
  if (which < PRIO_PROCESS || which > PRIO_USER) {
    errno = EINVAL;
    return -1;
  }
  errno = ENOTSUP;
  return -1;
}
