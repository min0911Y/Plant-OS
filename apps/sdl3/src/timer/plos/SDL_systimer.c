/* Plant OS timer backend. SDL owns the tick epoch and unit conversions. */
#include "../SDL_timer_c.h"
#include "SDL_internal.h"
#include <syscall.h>
#include <time.h>

Uint64 SDL_GetPerformanceCounter(void) { return monotonic_ns(); }
Uint64 SDL_GetPerformanceFrequency(void) { return SDL_NS_PER_SECOND; }

void SDL_SYS_DelayNS(Uint64 ns) {
  Uint64 milliseconds = ns / SDL_NS_PER_MS + (ns % SDL_NS_PER_MS != 0);
  if (!milliseconds) {
    api_yield();
    return;
  }
  while (milliseconds) {
    int chunk = (int)SDL_min(milliseconds, SDL_MAX_SINT32);
    sleep(chunk);
    milliseconds -= chunk;
  }
}
