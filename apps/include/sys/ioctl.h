#ifndef PLANT_SYS_IOCTL_H
#define PLANT_SYS_IOCTL_H

#define FIONREAD 0x4004667ful
#define SIOCGIFCONF 0x8912ul
#define SIOCGIFFLAGS 0x8913ul
#define SIOCGIFADDR 0x8915ul
#define SIOCGIFBRDADDR 0x8919ul
#define SIOCGIFNETMASK 0x891bul
#define SIOCGIFHWADDR 0x8927ul
#define SIOCGIFMTU 0x8921ul
#define SIOCGIFINDEX 0x8933ul
#define TIOCGWINSZ 0x5413ul

struct winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int descriptor, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif
