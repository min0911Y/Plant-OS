#include <dos.h>
#include <calendar.h>

#define CALENDAR_DAY_SECONDS 86400u
#define CALENDAR_COMMON_YEAR_SECONDS 31536000u
#define CALENDAR_LEAP_YEAR_SECONDS 31622400u

static const uint8_t calendar_common_month_days[] =
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const uint8_t calendar_leap_month_days[] =
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool calendar_is_leap_year(uint32_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint32_t calendar_to_unix_timestamp(uint32_t year, uint32_t month,
                                    uint32_t day, uint32_t hour,
                                    uint32_t minute, uint32_t second) {
  uint32_t leap_years = 0;
  for (uint32_t current = 1970; current < year; current++) {
    if (calendar_is_leap_year(current)) {
      leap_years++;
    }
  }

  const uint8_t *month_days = calendar_is_leap_year(year)
                                  ? calendar_leap_month_days
                                  : calendar_common_month_days;
  uint32_t seconds = 0;
  for (uint32_t index = 0; index + 1 < month; index++) {
    seconds += month_days[index] * CALENDAR_DAY_SECONDS;
  }
  seconds += (day - 1) * CALENDAR_DAY_SECONDS + hour * 3600 + minute * 60 +
             second;
  return leap_years * CALENDAR_LEAP_YEAR_SECONDS +
         (year - 1970 - leap_years) * CALENDAR_COMMON_YEAR_SECONDS + seconds;
}

void calendar_from_ntp_timestamp(uint32_t timestamp, uint32_t *year,
                                 uint32_t *month, uint32_t *day,
                                 uint32_t *hour, uint32_t *minute,
                                 uint32_t *second) {
  timestamp += 28800;
  uint32_t current_year = 1900;
  for (;;) {
    uint32_t year_seconds = calendar_is_leap_year(current_year)
                                ? CALENDAR_LEAP_YEAR_SECONDS
                                : CALENDAR_COMMON_YEAR_SECONDS;
    if (timestamp < year_seconds) {
      break;
    }
    timestamp -= year_seconds;
    current_year++;
  }

  const uint8_t *month_days = calendar_is_leap_year(current_year)
                                  ? calendar_leap_month_days
                                  : calendar_common_month_days;
  uint32_t current_month = 0;
  while (current_month + 1 < 12 &&
         timestamp >= month_days[current_month] * CALENDAR_DAY_SECONDS) {
    timestamp -= month_days[current_month] * CALENDAR_DAY_SECONDS;
    current_month++;
  }

  *year = current_year;
  *month = current_month + 1;
  *day = timestamp / CALENDAR_DAY_SECONDS + 1;
  timestamp %= CALENDAR_DAY_SECONDS;
  *hour = timestamp / 3600;
  timestamp %= 3600;
  *minute = timestamp / 60;
  *second = timestamp % 60;
}

unsigned time(void) {
  extern struct TIMERCTL timerctl;
  unsigned int t;
  t = get_year() * get_mon_hex() * get_day_of_month() * get_hour_hex() * get_min_hex() * get_sec_hex() + timerctl.count + timerctl.next;
  t |= (uint32_t)(uintptr_t)timerctl.t0;
  t |= get_day_of_week();
  
  // printk("%08x\n", t);
  return t;
}
