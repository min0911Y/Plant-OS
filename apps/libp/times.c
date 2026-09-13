#include <errno.h>
#include <sys/times.h>

clock_t times(struct tms *times) {
  errno = ENOTSUP;
  return (clock_t)-1;
}
