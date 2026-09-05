/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* Plant OS uses the kernel's monotonic nanosecond clock on both architectures. */
#include "../../SDL_internal.h"
#if SDL_TIMER_PLOS
#include "../SDL_timer_c.h"
#include <syscall.h>
#include <time.h>

static Uint64 start;
static SDL_bool initialized;

void SDL_TicksInit(void) {
  if (!initialized) {
    start = monotonic_ns();
    initialized = SDL_TRUE;
  }
}

void SDL_TicksQuit(void) { initialized = SDL_FALSE; }

Uint64 SDL_GetTicks64(void) {
  SDL_TicksInit();
  return (monotonic_ns() - start) / 1000000;
}

Uint32 SDL_GetTicks(void) { return (Uint32)SDL_GetTicks64(); }
Uint64 SDL_GetPerformanceCounter(void) { return monotonic_ns(); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000; }
void SDL_Delay(Uint32 milliseconds) {
  if (milliseconds)
    sleep(milliseconds);
  else
    api_yield();
}
#endif
