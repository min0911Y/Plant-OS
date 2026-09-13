#ifndef PLANT_NET_IF_DL_H
#define PLANT_NET_IF_DL_H

#include <socket.h>

#define AF_LINK 18

struct sockaddr_dl {
  uint8_t sdl_len;
  sa_family_t sdl_family;
  uint16_t sdl_index;
  uint8_t sdl_type;
  uint8_t sdl_nlen;
  uint8_t sdl_alen;
  uint8_t sdl_slen;
  char sdl_data[24];
};

#endif
