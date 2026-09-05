#include <arch/x86/io.h>
#include <calendar.h>
#include <dos.h>
#include <irq.h>
#include <platform.h>
#include <platform/pc.h>

#define RTC_STATUS_A 0x0a
#define RTC_STATUS_B 0x0b
#define RTC_UPDATING 0x80
#define RTC_BINARY 0x04
#define RTC_24_HOUR 0x02

unsigned char read_cmos(unsigned char index) {
  irq_state_t state = irq_save();
  x86_port_write8(CMOS_INDEX_PORT, index);
  unsigned char value = x86_port_read8(CMOS_DATA_PORT);
  irq_restore(state);
  return value;
}

void write_cmos(unsigned char index, unsigned char value) {
  irq_state_t state = irq_save();
  x86_port_write8(CMOS_INDEX_PORT, index);
  x86_port_write8(CMOS_DATA_PORT, value);
  irq_restore(state);
}

static unsigned rtc_decode(unsigned value, unsigned status) {
  return status & RTC_BINARY ? value : BCD_HEX(value);
}

/* Compare complete snapshots so a rollover cannot mix two calendar dates. */
bool platform_rtc_timestamp(uint32_t *timestamp) {
  static const uint8_t registers[] = {CMOS_CUR_SEC, CMOS_CUR_MIN, CMOS_CUR_HOUR,
                                      CMOS_MON_DAY, CMOS_CUR_MON, CMOS_CUR_YEAR,
                                      CMOS_CUR_CEN, RTC_STATUS_B};
  uint8_t previous[sizeof(registers)], current[sizeof(registers)];
  irq_state_t state = irq_save();
  bool have_previous = false;
  bool stable = false;
  for (unsigned attempt = 0; attempt < 8; attempt++) {
    if (read_cmos(RTC_STATUS_A) & RTC_UPDATING)
      break;
    for (size_t i = 0; i < sizeof(registers); i++)
      current[i] = read_cmos(registers[i]);
    if (read_cmos(RTC_STATUS_A) & RTC_UPDATING)
      break;
    if (have_previous && !memcmp(previous, current, sizeof(current))) {
      stable = true;
      break;
    }
    memcpy(previous, current, sizeof(current));
    have_previous = true;
  }
  irq_restore(state);
  if (!stable)
    return false;
  unsigned status = current[7];
  unsigned second = rtc_decode(current[0], status);
  unsigned minute = rtc_decode(current[1], status);
  unsigned hour = rtc_decode(current[2] & 0x7f, status);
  if (!(status & RTC_24_HOUR))
    hour = hour % 12 + ((current[2] & 0x80) ? 12 : 0);
  unsigned day = rtc_decode(current[3], status);
  unsigned month = rtc_decode(current[4], status);
  unsigned century = rtc_decode(current[6], status);
  /* PC firmware without a century register uses the 1970..2069 window. */
  unsigned year = rtc_decode(current[5], status);
  year += century >= 19 && century <= 99 ? century * 100
          : year >= 70                   ? 1900
                                         : 2000;
  if (year < 1970 || year > 2106 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour > 23 || minute > 59 || second > 59)
    return false;
  *timestamp =
      calendar_to_unix_timestamp(year, month, day, hour, minute, second);
  return true;
}

static unsigned rtc_field(unsigned index) {
  irq_state_t state = irq_save();
  unsigned status = read_cmos(RTC_STATUS_B);
  unsigned raw = read_cmos(index);
  unsigned value =
      rtc_decode(index == CMOS_CUR_HOUR ? raw & 0x7f : raw, status);
  if (index == CMOS_CUR_HOUR && !(status & RTC_24_HOUR))
    value = value % 12 + ((raw & 0x80) ? 12 : 0);
  irq_restore(state);
  return value;
}

unsigned int get_hour_hex(void) { return rtc_field(CMOS_CUR_HOUR); }
unsigned int get_min_hex(void) { return rtc_field(CMOS_CUR_MIN); }
unsigned int get_sec_hex(void) { return rtc_field(CMOS_CUR_SEC); }
unsigned int get_day_of_month(void) { return rtc_field(CMOS_MON_DAY); }
unsigned int get_day_of_week(void) { return rtc_field(CMOS_WEEK_DAY); }
unsigned int get_mon_hex(void) { return rtc_field(CMOS_CUR_MON); }
unsigned int get_year(void) {
  unsigned year = rtc_field(CMOS_CUR_YEAR);
  unsigned century = rtc_field(CMOS_CUR_CEN);
  return year + (century >= 19 && century <= 99 ? century * 100
                 : year >= 70                   ? 1900
                                                : 2000);
}

void write_cmos_time(unsigned int year, unsigned char month, unsigned char day,
                     unsigned char hour, unsigned char minute) {
  irq_state_t state = irq_save();
  unsigned status = read_cmos(RTC_STATUS_B);
  write_cmos(RTC_STATUS_B,
             status | 0x80); /* Freeze updates until all fields are set. */
  unsigned pm = 0;
  if (!(status & RTC_24_HOUR)) {
    pm = hour >= 12 ? 0x80 : 0;
    hour = hour % 12 ? hour % 12 : 12;
  }
  const uint8_t registers[] = {CMOS_CUR_HOUR, CMOS_CUR_MIN, CMOS_CUR_YEAR,
                               CMOS_CUR_CEN,  CMOS_CUR_MON, CMOS_MON_DAY};
  const unsigned values[] = {hour, minute, year % 100, year / 100, month, day};
  for (size_t i = 0; i < sizeof(registers); i++) {
    unsigned value = status & RTC_BINARY ? values[i] : HEX_BCD(values[i]);
    write_cmos(registers[i], value | (i == 0 ? pm : 0));
  }
  write_cmos(RTC_STATUS_B, status);
  irq_restore(state);
}
