#include <errno.h>
#include <limits.h>
#include <rand.h>
#include <time.h>

/* The RTC stores UTC; the system's local timezone is UTC+08:00. */
#define LOCAL_OFFSET_SECONDS (8 * 60 * 60)
#define DAY_SECONDS 86400

static struct tm calendar;

static struct tm *breakdown(int64_t seconds, struct tm *result) {
  int64_t days = seconds / DAY_SECONDS;
  int remainder = seconds % DAY_SECONDS;
  if (remainder < 0) {
    remainder += DAY_SECONDS;
    days--;
  }
  result->tm_hour = remainder / 3600;
  result->tm_min = remainder / 60 % 60;
  result->tm_sec = remainder % 60;
  result->tm_wday = ((days + 4) % 7 + 7) % 7;

  /* Gregorian calendar, decomposed into 400-year eras starting in March. */
  int64_t civil = days + 719468;
  int64_t era = (civil >= 0 ? civil : civil - 146096) / 146097;
  unsigned day_of_era = civil - era * 146097;
  unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
                          day_of_era / 146096) /
                         365;
  int year = year_of_era + era * 400;
  unsigned day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  unsigned march_month = (5 * day_of_year + 2) / 153;
  result->tm_mday = day_of_year - (153 * march_month + 2) / 5 + 1;
  result->tm_mon = march_month + (march_month < 10 ? 2 : -10);
  year += result->tm_mon < 2;
  result->tm_year = year - 1900;
  static const int month_days[] = {0,   31,  59,  90,  120, 151,
                                   181, 212, 243, 273, 304, 334};
  int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  result->tm_yday = month_days[result->tm_mon] + result->tm_mday - 1 +
                    (leap && result->tm_mon >= 2);
  result->tm_isdst = 0;
  return result;
}

time_t time(time_t *timer) {
  time_t value = RAND();
  if (timer)
    *timer = value;
  return value;
}

struct tm *gmtime_r(const time_t *timer, struct tm *result) {
  return breakdown(*timer, result);
}

struct tm *localtime_r(const time_t *timer, struct tm *result) {
  return breakdown((int64_t)*timer + LOCAL_OFFSET_SECONDS, result);
}

struct tm *gmtime(const time_t *timer) {
  return gmtime_r(timer, &calendar);
}
struct tm *localtime(const time_t *timer) {
  return localtime_r(timer, &calendar);
}

time_t mktime(struct tm *tm) {
  int64_t year = (int64_t)tm->tm_year + 1900 + tm->tm_mon / 12;
  int month = tm->tm_mon % 12;
  if (month < 0) {
    month += 12;
    year--;
  }
  year -= month < 2;
  int64_t era = (year >= 0 ? year : year - 399) / 400;
  unsigned year_of_era = year - era * 400;
  unsigned march_month = month + (month < 2 ? 10 : -2);
  int64_t days = era * 146097 + year_of_era * 365 + year_of_era / 4 -
                 year_of_era / 100 + (153 * march_month + 2) / 5 +
                 (int64_t)tm->tm_mday - 1 - 719468;
  int64_t seconds = days * DAY_SECONDS + (int64_t)tm->tm_hour * 3600 +
                    (int64_t)tm->tm_min * 60 + tm->tm_sec -
                    LOCAL_OFFSET_SECONDS;
  if (seconds < 0 || seconds >= UINT_MAX) {
    errno = EOVERFLOW;
    return (time_t)-1;
  }
  time_t result = seconds;
  localtime_r(&result, tm);
  return result;
}

double difftime(time_t end, time_t beginning) {
  return (double)end - (double)beginning;
}

void clock_gettime(int *seconds, int *microseconds) {
  uint64_t nanoseconds = monotonic_ns();
  *seconds = (int)(nanoseconds / 1000000000ull);
  *microseconds = (int)((nanoseconds % 1000000000ull) / 1000ull);
}
