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

static void cmos_w(u8 reg, u8 v)
{
    outb(0x70, reg);
    io_wait();
    outb(0x71, v);
}

static u8 tobcd(u8 v)
{
    return (u8)((v / 10) << 4 | (v % 10));
}

void rtc_write(const rtc_time_t *t)
{
    u8 b = cmos(0x0B);
    cmos_w(0x0B, (u8)(b | 0x80 | 0x02));                  /* остановить обновление, 24-часовой режим */
    b |= 0x02;
    int bin = b & 0x04;
    u8 yr = (u8)(t->year % 100);
    cmos_w(0x00, bin ? t->sec : tobcd(t->sec));
    cmos_w(0x02, bin ? t->min : tobcd(t->min));
    cmos_w(0x04, bin ? t->hour : tobcd(t->hour));
    cmos_w(0x07, bin ? t->day : tobcd(t->day));
    cmos_w(0x08, bin ? t->month : tobcd(t->month));
    cmos_w(0x09, bin ? yr : tobcd(yr));
    cmos_w(0x0B, (u8)(b & ~0x80));                        /* часы снова идут */
}
