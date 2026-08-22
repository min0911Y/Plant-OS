#define MAKE_LUAC
#include <signal.h>
static inline char *getenv(const char *s) { return "?.lua"; }
#include "lua/m.c"
