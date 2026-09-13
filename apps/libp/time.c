#include <errno.h>
#include <futex.h>
#include <limits.h>
#include <pthread.h>
#include <rand.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <wchar.h>

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
  int64_t year = year_of_era + era * 400;
  unsigned day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  unsigned march_month = (5 * day_of_year + 2) / 153;
  result->tm_mday = day_of_year - (153 * march_month + 2) / 5 + 1;
  result->tm_mon = march_month + (march_month < 10 ? 2 : -10);
  year += result->tm_mon < 2;
  if (year - 1900 < INT_MIN || year - 1900 > INT_MAX) {
    errno = EOVERFLOW;
    return NULL;
  }
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
  if (!breakdown(*timer, result))
    return NULL;
  result->tm_gmtoff = 0;
  result->tm_zone = "UTC";
  return result;
}

struct tm *localtime_r(const time_t *timer, struct tm *result) {
  if (!breakdown((int64_t)*timer + LOCAL_OFFSET_SECONDS, result))
    return NULL;
  result->tm_gmtoff = LOCAL_OFFSET_SECONDS;
  result->tm_zone = "CST";
  return result;
}

struct tm *gmtime(const time_t *timer) {
  return gmtime_r(timer, &calendar);
}
struct tm *localtime(const time_t *timer) {
  return localtime_r(timer, &calendar);
}

static time_t make_time(struct tm *tm, int offset) {
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
                    (int64_t)tm->tm_min * 60 + tm->tm_sec - offset;
  time_t result = seconds;
  if (!breakdown(seconds + offset, tm))
    return (time_t)-1;
  tm->tm_gmtoff = offset;
  tm->tm_zone = offset ? "CST" : "UTC";
  return result;
}
time_t mktime(struct tm *tm) { return make_time(tm, LOCAL_OFFSET_SECONDS); }

double difftime(time_t end, time_t beginning) {
  return (double)end - (double)beginning;
}

time_t timegm(struct tm *value) { return make_time(value, 0); }

static pthread_once_t realtime_once = PTHREAD_ONCE_INIT;
static uint64_t realtime_epoch;

static void initialize_realtime(void) {
  realtime_epoch = (uint64_t)time(NULL) * 1000000000ull - monotonic_ns();
}

int clock_gettime(clockid_t clock, struct timespec *value) {
  if (!value || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)) {
    errno = EINVAL;
    return -1;
  }
  if (clock == CLOCK_REALTIME)
    pthread_once(&realtime_once, initialize_realtime);
  uint64_t ns = monotonic_ns();
  if (clock == CLOCK_REALTIME)
    ns += realtime_epoch;
  value->tv_sec = ns / 1000000000ull;
  value->tv_nsec = ns % 1000000000ull;
  return 0;
}
int clock_getres(clockid_t clock, struct timespec *value) {
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) {
    errno = EINVAL;
    return -1;
  }
  if (value)
    *value = (struct timespec){0, 1};
  return 0;
}
int timespec_get(struct timespec *value, int base) {
  return base == TIME_UTC && clock_gettime(CLOCK_REALTIME, value) == 0 ? base
                                                                       : 0;
}

int gettimeofday(struct timeval *value, void *zone) {
  struct timespec now;
  if (value) {
    if (clock_gettime(CLOCK_REALTIME, &now))
      return -1;
    *value = (struct timeval){now.tv_sec, now.tv_nsec / 1000};
  }
  if (zone)
    *(struct timezone *)zone = (struct timezone){-LOCAL_OFFSET_SECONDS / 60, 0};
  return 0;
}

int runtime_deadline(clockid_t clock, const struct timespec *time,
                     uint64_t *deadline) {
  if (!time || time->tv_nsec < 0 || time->tv_nsec >= 1000000000 ||
      (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC))
    return EINVAL;
  if (time->tv_sec < 0) {
    *deadline = 0;
    return 0;
  }
  if ((uint64_t)time->tv_sec > (UINT64_MAX - 1 - time->tv_nsec) / 1000000000ull)
    return EOVERFLOW;
  uint64_t ns = (uint64_t)time->tv_sec * 1000000000ull + time->tv_nsec;
  if (clock == CLOCK_REALTIME) {
    pthread_once(&realtime_once, initialize_realtime);
    ns = ns > realtime_epoch ? ns - realtime_epoch : 0;
  }
  *deadline = ns;
  return 0;
}

int clock_nanosleep(clockid_t clock, int flags, const struct timespec *duration,
                    struct timespec *remaining) {
  if (flags & ~TIMER_ABSTIME || !duration || duration->tv_sec < 0 ||
      duration->tv_nsec < 0 || duration->tv_nsec >= 1000000000 ||
      (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC))
    return EINVAL;
  uint64_t deadline;
  if (flags & TIMER_ABSTIME) {
    int error = runtime_deadline(clock, duration, &deadline);
    if (error)
      return error;
  } else {
    uint64_t now = monotonic_ns();
    if (now >= UINT64_MAX - 1 - duration->tv_nsec ||
        (uint64_t)duration->tv_sec >
            (UINT64_MAX - 1 - now - duration->tv_nsec) / 1000000000ull)
      return EOVERFLOW;
    deadline =
        now + (uint64_t)duration->tv_sec * 1000000000ull + duration->tv_nsec;
  }
  if (monotonic_ns() >= deadline)
    return 0;
  uint32_t word = 0;
  int result = os_futex_wait(&word, 0, deadline);
  if (result == FUTEX_TIMED_OUT)
    return 0;
  if (remaining && !(flags & TIMER_ABSTIME)) {
    uint64_t now = monotonic_ns();
    uint64_t ns = deadline > now ? deadline - now : 0;
    *remaining = (struct timespec){ns / 1000000000ull, ns % 1000000000ull};
  }
  return result == FUTEX_INTERRUPTED ? EINTR : EINVAL;
}
int nanosleep(const struct timespec *duration, struct timespec *remaining) {
  int result = clock_nanosleep(CLOCK_MONOTONIC, 0, duration, remaining);
  if (!result)
    return 0;
  errno = result;
  return -1;
}

size_t wcsftime(wchar_t *text, size_t size, const wchar_t *format,
                const struct tm *time) {
  if (!size)
    return 0;
  size_t format_size = wcstombs(NULL, format, 0);
  if (format_size == (size_t)-1 || format_size == SIZE_MAX)
    return 0;
  char *narrow_format = malloc(format_size + 1);
  if (!narrow_format)
    return 0;
  wcstombs(narrow_format, format, format_size + 1);

  if (size > SIZE_MAX / MB_LEN_MAX) {
    free(narrow_format);
    return 0;
  }
  char *narrow_text = malloc(size * MB_LEN_MAX);
  if (!narrow_text) {
    free(narrow_format);
    return 0;
  }
  size_t result = strftime(narrow_text, size * MB_LEN_MAX, narrow_format, time);
  if (result) {
    size_t wide_size = mbstowcs(NULL, narrow_text, 0);
    result = wide_size == (size_t)-1 || wide_size >= size
                 ? 0
                 : mbstowcs(text, narrow_text, size);
  }
  free(narrow_text);
  free(narrow_format);
  return result == (size_t)-1 ? 0 : result;
}

char *asctime_r(const struct tm *value, char *buffer) {
  static const char *const days[] = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
  static const char *const months[] = {"Jan", "Feb", "Mar", "Apr",
                                       "May", "Jun", "Jul", "Aug",
                                       "Sep", "Oct", "Nov", "Dec"};
  if (!value || !buffer || value->tm_wday < 0 || value->tm_wday >= 7 ||
      value->tm_mon < 0 || value->tm_mon >= 12) {
    errno = EINVAL;
    return NULL;
  }
  int64_t year = (int64_t)value->tm_year + 1900;
  if (year < 0 || year > 9999 ||
      snprintf(buffer, 26, "%s %s%3d %02d:%02d:%02d %04d\n",
               days[value->tm_wday], months[value->tm_mon], value->tm_mday,
               value->tm_hour, value->tm_min, value->tm_sec, (int)year) != 25) {
    errno = EOVERFLOW;
    return NULL;
  }
  return buffer;
}
char *asctime(const struct tm *value) {
  static char buffer[26];
  return asctime_r(value, buffer);
}
char *ctime_r(const time_t *value, char *buffer) {
  struct tm time;
  return localtime_r(value, &time) ? asctime_r(&time, buffer) : NULL;
}
char *ctime(const time_t *value) {
  static char buffer[26];
  return ctime_r(value, buffer);
}

void tzset(void) {}

int setitimer(int which, const struct itimerval *value,
              struct itimerval *old_value) {
  (void)which;
  (void)value;
  if (old_value != NULL) {
    *old_value = (struct itimerval){0};
  }
  errno = ENOTSUP;
  return -1;
}

int getitimer(int which, struct itimerval *value) {
  if (which < ITIMER_REAL || which > ITIMER_PROF || value == NULL) {
    errno = EINVAL;
    return -1;
  }
  *value = (struct itimerval){0};
  return 0;
}
