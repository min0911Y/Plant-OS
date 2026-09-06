/* Plant OS wall clock uses libp's UTC and local-time conversion. */
#include "../SDL_time_c.h"
#include "SDL_internal.h"
#include <time.h>

void SDL_GetSystemTimeLocalePreferences(SDL_DateFormat *date,
                                        SDL_TimeFormat *time) {
  if (date)
    *date = SDL_DATE_FORMAT_YYYYMMDD;
  if (time)
    *time = SDL_TIME_FORMAT_24HR;
}

bool SDL_GetCurrentTime(SDL_Time *ticks) {
  if (!ticks)
    return SDL_InvalidParamError("ticks");
  *ticks = (SDL_Time)time(NULL) * SDL_NS_PER_SECOND;
  return true;
}

bool SDL_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool local) {
  if (!dt)
    return SDL_InvalidParamError("dt");
  if (ticks < 0 || ticks / SDL_NS_PER_SECOND > (time_t)-1)
    return SDL_SetError("Time exceeds the Plant OS clock range");
  time_t seconds = (time_t)(ticks / SDL_NS_PER_SECOND);
  struct tm value, utc;
  if (local)
    localtime_r(&seconds, &value);
  else
    gmtime_r(&seconds, &value);
  gmtime_r(&seconds, &utc);
  *dt = (SDL_DateTime){.year = value.tm_year + 1900,
                       .month = value.tm_mon + 1,
                       .day = value.tm_mday,
                       .hour = value.tm_hour,
                       .minute = value.tm_min,
                       .second = value.tm_sec,
                       .nanosecond = ticks % SDL_NS_PER_SECOND,
                       .day_of_week = value.tm_wday,
                       .utc_offset = local ? (int)(seconds - mktime(&utc)) : 0};
  return true;
}
