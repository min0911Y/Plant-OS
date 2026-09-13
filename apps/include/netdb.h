#ifndef PLANT_NETDB_H
#define PLANT_NETDB_H

#include <socket.h>
#include <netinet/in.h>

#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02
#define NI_NOFQDN 0x04
#define NI_NAMEREQD 0x08
#define NI_DGRAM 0x10
#define NI_NUMERICSCOPE 0x20

#define NI_MAXHOST 1025
#define NI_MAXSERV 32

struct protoent {
  char *p_name;
  char **p_aliases;
  int p_proto;
};

#ifdef __cplusplus
extern "C" {
#endif

int getnameinfo(const struct sockaddr *address, socklen_t address_length,
                char *host, socklen_t host_length, char *service,
                socklen_t service_length, int flags);
const char *gai_strerror(int error);
struct protoent *getprotobyname(const char *name);

#ifdef __cplusplus
}
#endif

#endif
