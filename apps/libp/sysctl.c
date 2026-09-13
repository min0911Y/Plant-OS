#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

static int sysctl_read(void *destination, size_t *destination_size,
                       const void *source, size_t source_size) {
  if (!destination_size) {
    errno = EINVAL;
    return -1;
  }
  size_t available = *destination_size;
  *destination_size = source_size;
  if (!destination)
    return 0;
  if (available < source_size) {
    errno = ENOMEM;
    return -1;
  }
  memcpy(destination, source, source_size);
  return 0;
}

int sysctl(const int *name, unsigned name_length, void *old_value,
           size_t *old_length, const void *new_value, size_t new_length) {
  if (!name || name_length != 2 || new_value || new_length) {
    errno = EINVAL;
    return -1;
  }

  if (name[0] == CTL_HW) {
    if (name[1] == HW_NCPU) {
      int value = (int)sysconf(_SC_NPROCESSORS_ONLN);
      return sysctl_read(old_value, old_length, &value, sizeof(value));
    }
    if (name[1] == HW_PHYSMEM) {
      uint64_t value = mem_total();
      return sysctl_read(old_value, old_length, &value, sizeof(value));
    }
#if defined(__x86_64__)
    static const char machine[] = "x86_64";
#else
    static const char machine[] = "i386";
#endif
    if (name[1] == HW_MACHINE || name[1] == HW_MODEL)
      return sysctl_read(old_value, old_length, machine, sizeof(machine));
  } else if (name[0] == CTL_KERN) {
    if (name[1] == KERN_BOOTTIME) {
      struct timespec realtime;
      struct timespec uptime;
      if (clock_gettime(CLOCK_REALTIME, &realtime) ||
          clock_gettime(CLOCK_MONOTONIC, &uptime))
        return -1;
      int64_t nanoseconds = realtime.tv_nsec - uptime.tv_nsec;
      struct timeval value = {
          .tv_sec = realtime.tv_sec - uptime.tv_sec - (nanoseconds < 0),
          .tv_usec = (nanoseconds + (nanoseconds < 0 ? 1000000000 : 0)) / 1000};
      return sysctl_read(old_value, old_length, &value, sizeof(value));
    }
    static const char os_type[] = "Plant OS";
    static const char os_release[] = "0.7b";
    if (name[1] == KERN_OSTYPE)
      return sysctl_read(old_value, old_length, os_type, sizeof(os_type));
    if (name[1] == KERN_OSRELEASE)
      return sysctl_read(old_value, old_length, os_release,
                         sizeof(os_release));
  }

  errno = ENOTSUP;
  return -1;
}

int sysctlbyname(const char *name, void *old_value, size_t *old_length,
                 const void *new_value, size_t new_length) {
  if (!name || new_value || new_length) {
    errno = EINVAL;
    return -1;
  }
  if (!strcmp(name, "vm.swapusage")) {
    xsw_usage value = {.xsu_pagesize = (uint32_t)getpagesize()};
    return sysctl_read(old_value, old_length, &value, sizeof(value));
  }
  errno = ENOTSUP;
  return -1;
}
