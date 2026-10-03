#include "rtc.h"
#include "io.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int update_in_progress(void) {
    outb(0x70, 0x0A);
    return inb(0x71) & 0x80;
}

static uint8_t bcd2bin(uint8_t v) {
    return (v & 0x0F) + ((v >> 4) * 10);
}

void rtc_read(struct rtc_time* t) {
    while (update_in_progress());

    uint8_t sec  = cmos_read(0x00);
    uint8_t min  = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t wday = cmos_read(0x06);
    uint8_t day  = cmos_read(0x07);
    uint8_t mon  = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);

    uint8_t regB = cmos_read(0x0B);
    int is_bcd = !(regB & 0x04);

    if (is_bcd) {
        sec  = bcd2bin(sec);
        min  = bcd2bin(min);
        uint8_t h12 = hour & 0x80;
        hour = bcd2bin(hour & 0x7F);
        if (h12) hour = ((hour + 12) % 24);
        wday = bcd2bin(wday);
        day  = bcd2bin(day);
        mon  = bcd2bin(mon);
        year = bcd2bin(year);
    }

    /* 12-часовой формат (если regB bit1 = 0) */
    if (!(regB & 0x02)) {
        uint8_t pm = hour & 0x80;
        hour = hour & 0x7F;
        if (pm && hour < 12) hour += 12;
        if (!pm && hour == 12) hour = 0;
    }

    t->second  = sec;
    t->minute  = min;
    t->hour    = hour;
    t->day     = day;
    t->month   = mon;
    t->year    = 2000 + year;
    t->weekday = (wday == 0) ? 0 : (wday - 1);
}

const char* rtc_month_name(int m) {
    static const char* names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (m < 1 || m > 12) return "???";
    return names[m - 1];
}

const char* rtc_weekday_name(int w) {
    static const char* names[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    if (w < 0 || w > 6) return "???";
    return names[w];
}
