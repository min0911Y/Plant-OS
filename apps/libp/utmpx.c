#include <stddef.h>
#include <utmpx.h>

void setutxent(void) {}

struct utmpx *getutxent(void) { return NULL; }

void endutxent(void) {}
