/* Часы реального времени (CMOS). */
#include "rtc.h"
#include "../kernel/io.h"

static u8 cmos(u8 reg)
{
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

static u8 bcd(u8 v)
{
    return (u8)((v & 0x0F) + (v >> 4) * 10);
}

void rtc_read(rtc_time_t *t)
{
    for (u32 i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++)
        ;                                  /* дождаться конца обновления часов */

    u8 sec = cmos(0x00), min = cmos(0x02), hour = cmos(0x04);
    u8 day = cmos(0x07), mon = cmos(0x08), year = cmos(0x09);
    u8 status_b = cmos(0x0B);

    int pm = (status_b & 0x02) == 0 && (hour & 0x80);
    hour &= 0x7F;

    if (!(status_b & 0x04)) {              /* значения в BCD */
        sec = bcd(sec); min = bcd(min); hour = bcd(hour);
        day = bcd(day); mon = bcd(mon); year = bcd(year);
    }
    if (!(status_b & 0x02)) {              /* 12-часовой режим */
        hour = (u8)(hour % 12);
        if (pm)
            hour = (u8)(hour + 12);
    }

    t->sec = sec; t->min = min; t->hour = hour;
    t->day = day; t->month = mon;
    t->year = (u16)(2000 + year);
}
