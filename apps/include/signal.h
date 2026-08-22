#ifndef __SIGNAL_H__
#define __SIGNAL_H__
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t handler);
#define SIGINT 0
#define SIGTERM 1
#define SIGILL 2
#define SIGPIPE 3
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#endif
