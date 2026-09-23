#include <socket.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int socket_syscall(unsigned operation, socket_syscall_request_t *request);

static int socket_call(unsigned operation, socket_syscall_request_t *request) {
  return socket_syscall(operation, request);
}

static int socket_result(int result) {
  if (result < 0) {
    errno = socket_error_number(result);
    return -1;
  }
  return result;
}

static int socket_connect_result(int result) {
  if (result == SOCKET_ERR_NOENT) {
    errno = ENOENT;
    return -1;
  }
  return socket_result(result);
}

static void socket_request_init(socket_syscall_request_t *request) {
  memset(request, 0, sizeof(*request));
}

socket_t socket(int domain, int type, int protocol) {
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.domain = domain;
  request.type = type;
  request.protocol = protocol;
  return socket_result(socket_call(SOCKET_SYSCALL_CREATE, &request));
}

int socket_close(socket_t socket) {
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  return socket_result(socket_call(SOCKET_SYSCALL_CLOSE, &request));
}

int getpeereid(int descriptor, uid_t *euid, gid_t *egid) {
  if (euid == NULL || egid == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (!socket_handle_is_tagged(descriptor)) {
    errno = EBADF;
    return -1;
  }
  struct sockaddr_storage peer;
  socklen_t length = sizeof(peer);
  if (getpeername(descriptor, (struct sockaddr *)&peer, &length) < 0) {
    return -1;
  }
  *euid = geteuid();
  *egid = getegid();
  return 0;
}

int bind(socket_t socket, const struct sockaddr *address, socklen_t length) {
  if (address == NULL) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.address = (uintptr_t)address;
  request.address_length = length;
  return socket_result(socket_call(SOCKET_SYSCALL_BIND, &request));
}

int connect(socket_t socket, const struct sockaddr *address, socklen_t length) {
  if (address == NULL) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.address = (uintptr_t)address;
  request.address_length = length;
  int result = socket_call(SOCKET_SYSCALL_CONNECT, &request);
  return socket_connect_result(result);
}

int listen(socket_t socket, int backlog) {
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.backlog = backlog;
  return socket_result(socket_call(SOCKET_SYSCALL_LISTEN, &request));
}

socket_t accept(socket_t socket, struct sockaddr *address, socklen_t *length) {
  if ((address == NULL) != (length == NULL)) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  if (address != NULL) {
    request.address = (uintptr_t)address;
    request.address_length = *length;
  }
  int result = socket_call(SOCKET_SYSCALL_ACCEPT, &request);
  if (result >= 0 && length != NULL) {
    *length = request.address_length;
  } else if (result < 0) {
    return socket_result(result);
  }
  return result;
}

int sendto(socket_t socket, const void *data, uint32_t length, uint32_t flags,
           const struct sockaddr *address, socklen_t address_length) {
  if (length != 0 && data == NULL) {
    errno = EINVAL;
    return -1;
  }
  if ((address == NULL) != (address_length == 0)) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.buffer = (uintptr_t)data;
  request.length = length;
  request.flags = flags;
  request.address = (uintptr_t)address;
  request.address_length = address_length;
  return socket_result(socket_call(SOCKET_SYSCALL_SENDTO, &request));
}

int send(socket_t socket, const void *data, uint32_t length, uint32_t flags) {
  return sendto(socket, data, length, flags, NULL, 0);
}

int recvfrom(socket_t socket, void *data, uint32_t length, uint32_t flags,
             struct sockaddr *address, socklen_t *address_length) {
  if ((address == NULL) != (address_length == NULL) ||
      (length != 0 && data == NULL)) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.buffer = (uintptr_t)data;
  request.length = length;
  request.flags = flags;
  if (address != NULL) {
    request.address = (uintptr_t)address;
    request.address_length = *address_length;
  }
  int result = socket_call(SOCKET_SYSCALL_RECVFROM, &request);
  if (result >= 0 && address_length != NULL) {
    *address_length = request.address_length;
  }
  return socket_result(result);
}

int recv(socket_t socket, void *data, uint32_t length, uint32_t flags) {
  return recvfrom(socket, data, length, flags, NULL, NULL);
}

int socket_error_number(int result) {
  switch (result) {
  case SOCKET_ERR_INVAL:
    return EINVAL;
  case SOCKET_ERR_NOMEM:
    return ENOMEM;
  case SOCKET_ERR_NOENT:
    return EBADF;
  case SOCKET_ERR_AGAIN:
    return EAGAIN;
  case SOCKET_ERR_NOTCONN:
    return ENOTCONN;
  case SOCKET_ERR_TIMEDOUT:
    return ETIMEDOUT;
  case SOCKET_ERR_NETDOWN:
    return ENETDOWN;
  case SOCKET_ERR_ADDRINUSE:
    return EADDRINUSE;
  case SOCKET_ERR_BUSY:
    return EBUSY;
  case SOCKET_ERR_PROTOCOL:
    return ENOPROTOOPT;
  case SOCKET_ERR_INPROGRESS:
    return EINPROGRESS;
  case SOCKET_ERR_PIPE:
    return EPIPE;
  default:
    return EIO;
  }
}

static int socket_iov_total(const struct iovec *vectors, int count,
                            size_t *total) {
  if (count < 0 || count > IOV_MAX || (count != 0 && vectors == NULL)) {
    return EINVAL;
  }
  size_t length = 0;
  for (int index = 0; index < count; index++) {
    if (vectors[index].iov_len != 0 && vectors[index].iov_base == NULL) {
      return EFAULT;
    }
    if (vectors[index].iov_len > SIZE_MAX - length) {
      return EOVERFLOW;
    }
    length += vectors[index].iov_len;
  }
  if (length > UINT32_MAX || length > (size_t)__INT_MAX__) {
    return EOVERFLOW;
  }
  *total = length;
  return 0;
}

ssize_t sendmsg(socket_t socket, const struct msghdr *message,
                uint32_t flags) {
  if (message == NULL || message->msg_controllen != 0 ||
      (message->msg_name == NULL && message->msg_namelen != 0)) {
    errno = EINVAL;
    return -1;
  }

  size_t total;
  int error = socket_iov_total(message->msg_iov, (int)message->msg_iovlen,
                               &total);
  if (error != 0) {
    errno = error;
    return -1;
  }

  void *buffer = NULL;
  if (total != 0) {
    buffer = malloc(total);
    if (buffer == NULL) {
      errno = ENOMEM;
      return -1;
    }
  }
  size_t offset = 0;
  for (size_t index = 0; index < message->msg_iovlen; index++) {
    memcpy((uint8_t *)buffer + offset, message->msg_iov[index].iov_base,
           message->msg_iov[index].iov_len);
    offset += message->msg_iov[index].iov_len;
  }

  int result = sendto(socket, buffer, (uint32_t)total, flags,
                      (const struct sockaddr *)message->msg_name,
                      message->msg_namelen);
  free(buffer);
  if (result < 0) {
    return -1;
  }
  return result;
}

ssize_t recvmsg(socket_t socket, struct msghdr *message, uint32_t flags) {
  if (message == NULL || message->msg_controllen != 0 ||
      (message->msg_name == NULL && message->msg_namelen != 0)) {
    errno = EINVAL;
    return -1;
  }

  size_t total;
  int error = socket_iov_total(message->msg_iov, (int)message->msg_iovlen,
                               &total);
  if (error != 0) {
    errno = error;
    return -1;
  }

  void *buffer = NULL;
  if (total != 0) {
    buffer = malloc(total);
    if (buffer == NULL) {
      errno = ENOMEM;
      return -1;
    }
  }
  socklen_t address_length = message->msg_namelen;
  int result = recvfrom(socket, buffer, (uint32_t)total, flags,
                        (struct sockaddr *)message->msg_name,
                        message->msg_name == NULL ? NULL : &address_length);
  if (result < 0) {
    free(buffer);
    return -1;
  }
  if (message->msg_name != NULL) {
    message->msg_namelen = address_length;
  }

  size_t remaining = (size_t)result;
  size_t offset = 0;
  for (size_t index = 0; index < message->msg_iovlen && remaining != 0;
       index++) {
    size_t part = message->msg_iov[index].iov_len < remaining
                      ? message->msg_iov[index].iov_len
                      : remaining;
    memcpy(message->msg_iov[index].iov_base, (uint8_t *)buffer + offset, part);
    offset += part;
    remaining -= part;
  }
  free(buffer);
  message->msg_flags = 0;
  return result;
}

int socket_bytes_available(socket_t socket, uint32_t *bytes) {
  if (bytes == NULL) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  int result = socket_call(SOCKET_SYSCALL_AVAILABLE, &request);
  if (result >= 0)
    *bytes = request.length;
  return socket_result(result);
}

static int socket_getname(socket_t socket, struct sockaddr *address,
                          socklen_t *length, unsigned operation) {
  if (address == NULL || length == NULL) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.address = (uintptr_t)address;
  request.address_length = *length;
  int result = socket_call(operation, &request);
  if (result == 0) {
    *length = request.address_length;
  }
  return socket_result(result);
}

int getsockname(socket_t socket, struct sockaddr *address, socklen_t *length) {
  return socket_getname(socket, address, length, SOCKET_SYSCALL_GETSOCKNAME);
}

int getpeername(socket_t socket, struct sockaddr *address, socklen_t *length) {
  return socket_getname(socket, address, length, SOCKET_SYSCALL_GETPEERNAME);
}

int setsockopt(socket_t socket, int level, int option, const void *value,
               socklen_t length) {
  if (value == NULL || length == 0) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.domain = level;
  request.type = option;
  request.buffer = (uintptr_t)value;
  request.length = length;
  socket_syscall_timeval_t timeout;
  if (level == SOL_SOCKET && (option == SO_RCVTIMEO || option == SO_SNDTIMEO)) {
    if (length != sizeof(struct timeval)) {
      errno = EINVAL;
      return -1;
    }
    struct timeval time;
    memcpy(&time, value, sizeof(time));
    timeout.seconds = time.tv_sec;
    timeout.microseconds = time.tv_usec;
    request.buffer = (uintptr_t)&timeout;
    request.length = sizeof(timeout);
  }
  int result = socket_call(SOCKET_SYSCALL_SET_OPTION, &request);
  if (result < 0) {
    errno = socket_error_number(result);
    return -1;
  }
  return 0;
}

int getsockopt(socket_t socket, int level, int option, void *value,
               socklen_t *length) {
  if (value == NULL || length == NULL || *length == 0) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.domain = level;
  request.type = option;
  request.buffer = (uintptr_t)value;
  request.length = *length;
  bool is_timeout =
      level == SOL_SOCKET && (option == SO_RCVTIMEO || option == SO_SNDTIMEO);
  socket_syscall_timeval_t timeout;
  if (is_timeout) {
    if (*length < sizeof(struct timeval)) {
      errno = EINVAL;
      return -1;
    }
    request.buffer = (uintptr_t)&timeout;
    request.length = sizeof(timeout);
  }
  int result = socket_call(SOCKET_SYSCALL_GET_OPTION, &request);
  if (result < 0) {
    errno = socket_error_number(result);
    return -1;
  }
  if (is_timeout) {
    struct timeval time = {.tv_sec = timeout.seconds,
                           .tv_usec = timeout.microseconds};
    memcpy(value, &time, sizeof(time));
    request.length = sizeof(time);
  }
  *length = request.length;
  return 0;
}

int shutdown(socket_t socket, int how) {
  if (how < SHUT_RD || how > SHUT_RDWR) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.type = how;
  int result = socket_call(SOCKET_SYSCALL_SHUTDOWN, &request);
  if (result < 0) {
    errno = socket_error_number(result);
    return -1;
  }
  return 0;
}

int socketpair(int domain, int type, int protocol, socket_t sockets[2]) {
  if (sockets == NULL) {
    errno = EFAULT;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.domain = domain;
  request.type = type;
  request.protocol = protocol;
  request.buffer = (uintptr_t)sockets;
  request.length = sizeof(socket_t) * 2;
  int result = socket_call(SOCKET_SYSCALL_PAIR, &request);
  if (result < 0) {
    errno = socket_error_number(result);
    return -1;
  }
  return 0;
}

int socket_get_flags(socket_t socket) {
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  int result = socket_call(SOCKET_SYSCALL_GET_FLAGS, &request);
  if (result < 0) return socket_result(result);
  return (int)request.flags;
}

int socket_set_flags(socket_t socket, int flags) {
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.socket = socket;
  request.flags = (uint32_t)flags;
  return socket_result(socket_call(SOCKET_SYSCALL_SET_FLAGS, &request));
}

int inet_pton(int family, const char *text, void *address) {
  if (family != AF_INET || text == NULL || address == NULL) {
    return 0;
  }

  uint32_t value = 0;
  for (unsigned part = 0; part < 4; part++) {
    unsigned digits = 0;
    unsigned octet = 0;
    while (*text >= '0' && *text <= '9') {
      if (octet > 25 || (octet == 25 && *text > '5')) {
        return 0;
      }
      octet = octet * 10 + (unsigned)(*text - '0');
      text++;
      digits++;
    }
    if (digits == 0) {
      return 0;
    }
    value = (value << 8) | octet;
    if (part != 3) {
      if (*text != '.') {
        return 0;
      }
      text++;
    }
  }
  if (*text != '\0') {
    return 0;
  }
  ((struct in_addr *)address)->s_addr = htonl(value);
  return 1;
}

const char *inet_ntop(int family, const void *address, char *text,
                      socklen_t text_length) {
  if (family != AF_INET || address == NULL || text == NULL) {
    return NULL;
  }

  uint32_t value = ntohl(((const struct in_addr *)address)->s_addr);
  char buffer[INET_ADDRSTRLEN];
  char *out = buffer;
  for (unsigned part = 0; part < 4; part++) {
    unsigned octet = (value >> (24 - part * 8)) & 0xffu;
    if (octet >= 100) {
      *out++ = (char)('0' + octet / 100);
      octet %= 100;
      *out++ = (char)('0' + octet / 10);
    } else if (octet >= 10) {
      *out++ = (char)('0' + octet / 10);
    }
    *out++ = (char)('0' + octet % 10);
    if (part != 3) {
      *out++ = '.';
    }
  }
  *out = '\0';
  size_t length = (size_t)(out - buffer) + 1;
  if (text_length < length) {
    return NULL;
  }
  memcpy(text, buffer, length);
  return text;
}

static int socket_parse_service(const char *service, uint16_t *port) {
  if (service == NULL) {
    *port = 0;
    return 0;
  }
  if (*service == '\0') {
    return EAI_SERVICE;
  }
  uint32_t value = 0;
  while (*service != '\0') {
    if (*service < '0' || *service > '9' || value > 6553 ||
        (value == 6553 && *service > '5')) {
      return EAI_SERVICE;
    }
    value = value * 10 + (unsigned)(*service - '0');
    service++;
  }
  *port = (uint16_t)value;
  return 0;
}

static int socket_resolve_ipv4(const char *name, struct in_addr *address) {
  size_t length = strlen(name) + 1;
  if (length > 255) {
    return SOCKET_ERR_INVAL;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.buffer = (uintptr_t)name;
  request.length = length;
  request.address = (uintptr_t)&address->s_addr;
  return socket_call(SOCKET_SYSCALL_RESOLVE, &request);
}

static int socket_gai_error(int error) {
  if (error == SOCKET_ERR_NOMEM) {
    return EAI_MEMORY;
  }
  if (error == SOCKET_ERR_AGAIN || error == SOCKET_ERR_NETDOWN ||
      error == SOCKET_ERR_TIMEDOUT) {
    return EAI_AGAIN;
  }
  return EAI_NONAME;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result) {
  if (result == NULL) {
    return EAI_FAIL;
  }
  *result = NULL;

  int flags = hints == NULL ? 0 : hints->ai_flags;
  int family = hints == NULL ? AF_UNSPEC : hints->ai_family;
  if ((flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                 AI_NUMERICSERV | AI_V4MAPPED | AI_ALL)) != 0) {
    return EAI_BADFLAGS;
  }
  if (family != AF_UNSPEC && family != AF_INET) {
    return EAI_FAMILY;
  }

  uint16_t port;
  int service_result = socket_parse_service(service, &port);
  if (service_result != 0) {
    return service_result;
  }

  struct in_addr resolved;
  if (node == NULL) {
    resolved.s_addr = INADDR_ANY;
  } else if (!inet_pton(AF_INET, node, &resolved)) {
    if (flags & AI_NUMERICHOST) {
      return EAI_NONAME;
    }
    int resolve_result = socket_resolve_ipv4(node, &resolved);
    if (resolve_result != 0) {
      return socket_gai_error(resolve_result);
    }
  }

  struct addrinfo *entry = malloc(sizeof(*entry));
  struct sockaddr_in *address = malloc(sizeof(*address));
  if (entry == NULL || address == NULL) {
    free(entry);
    free(address);
    return EAI_MEMORY;
  }
  memset(entry, 0, sizeof(*entry));
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_port = htons(port);
  address->sin_addr = resolved;
  entry->ai_flags = flags;
  entry->ai_family = AF_INET;
  entry->ai_socktype = hints == NULL ? 0 : hints->ai_socktype;
  entry->ai_protocol = hints == NULL ? 0 : hints->ai_protocol;
  entry->ai_addrlen = sizeof(*address);
  entry->ai_addr = (struct sockaddr *)address;

  if ((flags & AI_CANONNAME) && node != NULL) {
    size_t length = strlen(node) + 1;
    entry->ai_canonname = malloc(length);
    if (entry->ai_canonname == NULL) {
      free(address);
      free(entry);
      return EAI_MEMORY;
    }
    memcpy(entry->ai_canonname, node, length);
  }
  *result = entry;
  return 0;
}

void freeaddrinfo(struct addrinfo *result) {
  while (result != NULL) {
    struct addrinfo *next = result->ai_next;
    free(result->ai_canonname);
    free(result->ai_addr);
    free(result);
    result = next;
  }
}

int socket_interface_address(struct in_addr *address) {
  if (address == NULL) {
    errno = EINVAL;
    return -1;
  }
  socket_syscall_request_t request;
  socket_request_init(&request);
  request.address = (uintptr_t)&address->s_addr;
  return socket_result(
      socket_call(SOCKET_SYSCALL_INTERFACE_ADDRESS, &request));
}
