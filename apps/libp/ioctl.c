#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <socket.h>
#include <string.h>
#include <stdarg.h>
#include <sys/ioctl.h>

static struct ifaddrs *ioctl_find_interface(const char *name,
                                            struct ifaddrs **list) {
  if (getifaddrs(list) != 0) {
    return NULL;
  }
  for (struct ifaddrs *entry = *list; entry != NULL; entry = entry->ifa_next) {
    if (!strcmp(entry->ifa_name, name)) {
      return entry;
    }
  }
  freeifaddrs(*list);
  *list = NULL;
  errno = ENODEV;
  return NULL;
}

int ioctl(int descriptor, unsigned long request, ...) {
  if (request != FIONREAD && request != SIOCGIFFLAGS &&
      request != SIOCGIFMTU && request != SIOCGIFINDEX &&
      request != SIOCGIFADDR && request != SIOCGIFNETMASK &&
      request != SIOCGIFBRDADDR && request != SIOCGIFHWADDR &&
      request != SIOCGIFCONF) {
    errno = ENOTTY;
    return -1;
  }

  va_list arguments;
  va_start(arguments, request);
  void *result = va_arg(arguments, void *);
  va_end(arguments);
  if (!result) {
    errno = EFAULT;
    return -1;
  }

  if (request != FIONREAD) {
    if (!socket_handle_is_tagged(descriptor)) {
      errno = EBADF;
      return -1;
    }
    if (request == SIOCGIFCONF) {
      struct ifconf *configuration = result;
      if (configuration->ifc_buf == NULL || configuration->ifc_len < 0) {
        errno = EINVAL;
        return -1;
      }
      struct ifaddrs *list;
      if (getifaddrs(&list) != 0) {
        return -1;
      }
      int capacity = configuration->ifc_len;
      int count = 0;
      for (struct ifaddrs *entry = list; entry != NULL;
           entry = entry->ifa_next) {
        if (entry->ifa_addr == NULL ||
            entry->ifa_addr->sa_family != AF_INET ||
            capacity - count < (int)sizeof(struct ifreq)) {
          continue;
        }
        struct ifreq *copy = (struct ifreq *)(configuration->ifc_buf + count);
        memset(copy, 0, sizeof(*copy));
        strncpy(copy->ifr_name, entry->ifa_name, IFNAMSIZ - 1);
        memcpy(&copy->ifr_addr, entry->ifa_addr, sizeof(struct sockaddr));
        count += sizeof(struct ifreq);
      }
      configuration->ifc_len = count;
      freeifaddrs(list);
      return 0;
    }

    struct ifreq *ifreq = result;
    struct ifaddrs *list;
    struct ifaddrs *entry = ioctl_find_interface(ifreq->ifr_name, &list);
    if (entry == NULL) {
      return -1;
    }
    switch (request) {
    case SIOCGIFFLAGS:
      ifreq->ifr_flags = (short)entry->ifa_flags;
      break;
    case SIOCGIFMTU:
      ifreq->ifr_mtu = 1500;
      break;
    case SIOCGIFINDEX:
      ifreq->ifr_index = (int)if_nametoindex(entry->ifa_name);
      break;
    case SIOCGIFADDR:
      memcpy(&ifreq->ifr_addr, entry->ifa_addr, sizeof(struct sockaddr));
      break;
    case SIOCGIFNETMASK:
      memcpy(&ifreq->ifr_netmask, entry->ifa_netmask, sizeof(struct sockaddr));
      break;
    case SIOCGIFBRDADDR:
      memcpy(&ifreq->ifr_broadaddr, entry->ifa_broadaddr,
             sizeof(struct sockaddr));
      break;
    case SIOCGIFHWADDR:
      memset(&ifreq->ifr_hwaddr, 0, sizeof(ifreq->ifr_hwaddr));
      ifreq->ifr_hwaddr.sa_family = AF_LINK;
      break;
    default:
      freeifaddrs(list);
      errno = ENOTTY;
      return -1;
    }
    freeifaddrs(list);
    return 0;
  }

  uint32_t bytes;
  int error = socket_bytes_available(descriptor, &bytes);
  if (error) {
    errno = error == SOCKET_ERR_NOENT ? EBADF : EIO;
    return -1;
  }
  if (bytes > __INT_MAX__) {
    errno = EOVERFLOW;
    return -1;
  }
  *(int *)result = (int)bytes;
  return 0;
}
