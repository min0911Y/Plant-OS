#ifndef KERNEL_CALENDAR_H
#define KERNEL_CALENDAR_H

#include <ctypes.h>

uint32_t calendar_to_unix_timestamp(uint32_t year, uint32_t month,
                                    uint32_t day, uint32_t hour,
                                    uint32_t minute, uint32_t second);
void calendar_from_ntp_timestamp(uint32_t timestamp, uint32_t *year,
                                 uint32_t *month, uint32_t *day,
                                 uint32_t *hour, uint32_t *minute,
                                 uint32_t *second);

#endif
