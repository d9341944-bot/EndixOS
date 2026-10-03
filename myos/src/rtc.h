#ifndef RTC_H
#define RTC_H

struct rtc_time {
    int second, minute, hour;
    int day, month, year;
    int weekday;   /* 0=Sunday .. 6=Saturday */
};

void rtc_read(struct rtc_time* t);
const char* rtc_month_name(int m);
const char* rtc_weekday_name(int w);

#endif
