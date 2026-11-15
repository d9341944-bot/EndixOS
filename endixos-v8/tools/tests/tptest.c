/* Хост-тест жестов тачпада Synaptics: подаём готовые 6-байтовые пакеты, смотрим на события. */
extern int printf(const char *, ...);
#include "kernel/types.h"
#include "kernel/event.h"
void irq_register(int n, void (*f)(void)) { (void)n; (void)f; }
#include "drivers/ps2.c"
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static struct { int t; i32 a, b, c; } ev[256]; static int nev;
void event_push(int t, i32 a, i32 b, i32 c) { if (nev < 256) { ev[nev].t = t; ev[nev].a = a; ev[nev].b = b; ev[nev].c = c; nev++; } }
void pic_unmask(int n) { (void)n; }
void serial_puts(const char *s) { (void)s; } void serial_put_hex(u32 v) { (void)v; }
static void pk(i32 x, i32 y, i32 z, i32 w, int left)
{
    u8 b[6];
    b[0] = (u8)(0x80 | (((w >> 2) & 3) << 4) | (((w >> 1) & 1) << 2) | (left ? 1 : 0));
    b[1] = (u8)(((y >> 4) & 0xF0) | ((x >> 8) & 0x0F));
    b[2] = (u8)z;
    b[3] = (u8)(0xC0 | (((x >> 12) & 1) << 4) | (((y >> 12) & 1) << 5) | ((w & 1) << 2) | (left ? 1 : 0));
    b[4] = (u8)(x & 0xFF); b[5] = (u8)(y & 0xFF);
    for (int i = 0; i < 6; i++) mouse_byte(b[i]);
}
int main(void)
{
    ps2_set_bounds(1024, 600); tp_on = 1; pkt_size = 6; ps2_tp_set(50, 1, 1);
    /* движение одним пальцем вправо на 400 единиц */
    for (int i = 0; i < 20; i++) pk(2000 + i * 20, 2000, 60, 5, 0);
    CHECK(mx > 512 + 50 && my == 300, "палец вправо двигает курсор вправо");
    int moved = nev;
    /* подъём после длинного движения - не тап */
    pk(2400, 2000, 0, 0, 0);
    CHECK(nev == moved, "после движения подъём не даёт щелчка");
    /* тап */
    nev = 0;
    pk(3000, 2000, 60, 5, 0); pk(3002, 2000, 60, 5, 0); pk(3002, 2000, 60, 5, 0); pk(3002, 2000, 0, 0, 0);
    CHECK(nev == 2 && ev[0].c == 1 && ev[1].c == 0, "короткое касание = щелчок (нажатие и отпускание)");
    /* тап выключен */
    ps2_tp_set(50, 0, 1); nev = 0;
    pk(3000, 2000, 60, 5, 0); pk(3002, 2000, 60, 5, 0); pk(3002, 2000, 0, 0, 0);
    CHECK(nev == 0, "тап выключен - щелчка нет");
    ps2_tp_set(50, 1, 1);
    /* два пальца: касание = правый щелчок */
    nev = 0;
    pk(3000, 2000, 60, 0, 0); pk(3001, 2000, 60, 0, 0); pk(3001, 2000, 60, 0, 0); pk(3001, 2000, 0, 0, 0);
    CHECK(nev == 2 && ev[0].c == 2, "касание двумя пальцами = правый щелчок");
    /* прокрутка: два пальца вверх (y растёт), естественная - колесо вниз */
    nev = 0;
    for (int i = 0; i < 12; i++) pk(3000, 2000 + i * 30, 60, 0, 0);
    int dn = 0, up = 0; for (int i = 0; i < nev; i++) if (ev[i].t == EV_WHEEL) { if (ev[i].a < 0) dn++; else up++; }
    CHECK(dn >= 3 && up == 0, "двумя пальцами вверх: естественная прокрутка - колесо вниз");
    pk(3000, 3000, 0, 0, 0);
    ps2_tp_set(50, 1, 0); nev = 0;
    for (int i = 0; i < 12; i++) pk(3000, 2000 + i * 30, 60, 0, 0);
    up = 0; for (int i = 0; i < nev; i++) if (ev[i].t == EV_WHEEL && ev[i].a > 0) up++;
    CHECK(up >= 3, "обычная прокрутка: вверх");
    pk(3000, 3000, 0, 0, 0);
    /* физическая кнопка клик-пада */
    nev = 0; pk(3000, 2000, 60, 5, 1); pk(3000, 2000, 60, 5, 0);
    CHECK(nev >= 2 && ev[0].c == 1 && ev[1].c == 0, "кнопка клик-пада нажимается и отпускается");
    /* быстрое движение быстрее медленного */
    ps2_tp_set(50, 1, 1); pk(3000, 2000, 0, 0, 0); i32 a0 = mx;
    pk(1000, 2000, 60, 5, 0); for (int i = 1; i <= 10; i++) pk(1000 + i * 10, 2000, 60, 5, 0); i32 slow = mx - a0;
    pk(1000, 2000, 0, 0, 0); a0 = mx;
    pk(1000, 2000, 60, 5, 0); for (int i = 1; i <= 10; i++) pk(1000 + i * 100, 2000, 60, 5, 0); i32 fast = mx - a0;
    CHECK(fast > slow * 10, "ускорение: быстрое движение уходит дальше");
    printf("fails=%d\n", fails);
    return fails;
}
