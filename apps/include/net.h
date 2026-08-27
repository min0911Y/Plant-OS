#ifndef PLOS_NET_H
#define PLOS_NET_H

#ifdef __cplusplus
extern "C" {
#endif

#define UDP_PROTOCOL 17u
#define TCP_PROTOCOL 6u

typedef int socket_t;

socket_t Socket_Alloc(unsigned int protocol);
int Socket_Init(socket_t socket, unsigned int remote_ip,
                unsigned short remote_port, unsigned int local_ip,
                unsigned short local_port);
int Socket_Free(socket_t socket);
int Socket_Send(socket_t socket, const unsigned char *data, unsigned int size);
int Socket_Recv(socket_t socket, unsigned char *data, unsigned int size);
int listen(socket_t socket);
int connect(socket_t socket);
unsigned int GetIP(void);
int ping(unsigned int remote_ip);

#ifdef __cplusplus
}
#endif

#endif
