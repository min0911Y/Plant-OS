#ifndef PLANT_ARPA_INET_H
#define PLANT_ARPA_INET_H

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

int inet_pton(int family, const char *text, void *address);
const char *inet_ntop(int family, const void *address, char *text,
                      socklen_t text_length);

#ifdef __cplusplus
}
#endif

#endif
