#include <errno.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

int uname(struct utsname *name) {
  if (!name) {
    errno = EFAULT;
    return -1;
  }

  int os_type[] = {CTL_KERN, KERN_OSTYPE};
  int os_release[] = {CTL_KERN, KERN_OSRELEASE};
  int machine[] = {CTL_HW, HW_MACHINE};
  size_t length = sizeof(name->sysname);
  if (sysctl(os_type, 2, name->sysname, &length, NULL, 0))
    return -1;
  length = sizeof(name->release);
  if (sysctl(os_release, 2, name->release, &length, NULL, 0))
    return -1;
  length = sizeof(name->machine);
  if (sysctl(machine, 2, name->machine, &length, NULL, 0))
    return -1;
  if (gethostname(name->nodename, sizeof(name->nodename)))
    return -1;
  memcpy(name->version, name->release, strlen(name->release) + 1);
  return 0;
}
