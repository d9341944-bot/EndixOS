#ifndef ENDIX_RTC_H
#define ENDIX_RTC_H

#include "../kernel/types.h"

typedef struct {
    u8  sec, min, hour;
    u8  day, month;
    u16 year;
} rtc_time_t;

void rtc_read(rtc_time_t *t);
void rtc_write(const rtc_time_t *t);          /* записывает локальное время в CMOS (24-часовой режим) */

#endif
