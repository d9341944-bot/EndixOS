#include "gui/perf.c"
#include "gui/fb.c"
extern int printf(const char *, ...);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int main(void)
{
    u32 cpt = 16000000;                                  /* 1.6 ГГц: 16 млн тактов за 10 мс */
    perf_init(cpt);
    CHECK(perf_mode() == PERF_AUTO && perf_level() == 0, "старт: Auto, полное качество");
    for (int i = 0; i < 40; i++) perf_frame(cpt * 2);    /* кадры по 20 мс */
    CHECK(perf_level() == 0, "быстрые кадры качество не снижают");
    CHECK(perf_frame_ms() >= 18 && perf_frame_ms() <= 22, "время кадра измеряется (~20 мс)");
    for (int i = 0; i < 5; i++) perf_frame(cpt * 12);    /* 120 мс */
    CHECK(perf_level() <= 1, "несколько долгих кадров ещё не снижают качество");
    for (int i = 0; i < 12; i++) perf_frame(cpt * 12);
    CHECK(perf_level() >= 1 && fb_perf_get() == perf_level(), "долгие кадры: качество снижено");
    for (int i = 0; i < 60; i++) perf_frame(cpt * 12);
    CHECK(perf_level() == 2, "самый тяжёлый случай: уровень 2");
    for (int i = 0; i < 60; i++) perf_frame(1000);
    CHECK(perf_level() == 2, "в Auto качество не возвращается само (нет мерцания)");
    perf_set_mode(PERF_HIGH);
    CHECK(perf_level() == 0 && fb_perf_get() == 0, "High: полное качество");
    for (int i = 0; i < 80; i++) perf_frame(cpt * 12);
    CHECK(perf_level() == 0, "High не снижается даже на медленных кадрах");
    perf_set_mode(PERF_BALANCED); CHECK(perf_level() == 1, "Balanced = уровень 1");
    perf_set_mode(PERF_FAST); CHECK(perf_level() == 2, "Fast = уровень 2");
    perf_set_mode(99); CHECK(perf_mode() == PERF_FAST, "неверный режим игнорируется");
    return fails;
}
