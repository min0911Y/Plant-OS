#ifndef PLANT_POLL_H
#define PLANT_POLL_H

#include <sys/types.h>

typedef unsigned long nfds_t;

#include <poll_abi.h>

#ifdef __cplusplus
extern "C" {
#endif
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
#ifdef __cplusplus
}
#endif

#endif
