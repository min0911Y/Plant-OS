#ifndef PLANT_TERMIOS_H
#define PLANT_TERMIOS_H

#include <sys/types.h>

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_cc[32];
};

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ECHO 0000010

#ifdef __cplusplus
extern "C" {
#endif

int tcgetattr(int descriptor, struct termios *attributes);
int tcsetattr(int descriptor, int actions, const struct termios *attributes);

#ifdef __cplusplus
}
#endif

#endif
