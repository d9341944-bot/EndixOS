#include "perf.h"
#include "fb.h"

#define SLOW_MS 40                       /* дольше - рывки (меньше 25 кадров/с) */
#define NEED_FRAMES 6                    /* сколько долгих кадров подряд, чтобы снизить качество */

static u32 cpt = 1;
static int mode = PERF_AUTO, level, ema16, since, slow;       /* ema16: сглаженное время кадра в 1/16 мс */

void perf_init(u32 cycles_per_tick) { cpt = cycles_per_tick ? cycles_per_tick : 1; level = mode == PERF_BALANCED ? 1 : mode == PERF_FAST ? 2 : 0; fb_perf_set(level); }
int  perf_mode(void) { return mode; }
int  perf_level(void) { return level; }
int  perf_frame_ms(void) { return (ema16 + 8) / 16; }

void perf_set_mode(int m)
{
    if (m < 0 || m >= PERF_MODES) return;
    mode = m; slow = 0; since = 0; ema16 = 0;
    level = m == PERF_BALANCED ? 1 : m == PERF_FAST ? 2 : 0;       /* Auto начинает с полного качества и только снижает */
    fb_perf_set(level);
}

void perf_frame(u32 cycles)
{
    int ms = (int)(cycles / cpt * 10 + (cycles % cpt) * 10 / cpt);   /* кадров по 10 мс на тик */
    ema16 += (ms * 16 - ema16) / 4;
    if (mode != PERF_AUTO || level >= 2) return;
    since++;
    if (ema16 > SLOW_MS * 16) slow++; else slow = 0;
    if (slow >= NEED_FRAMES && since >= NEED_FRAMES) {
        level++; slow = 0; since = 0;
        ema16 = ema16 * 2 / 3;                                     /* следующий уровень примерно так же быстрее */
        fb_perf_set(level);
    }
}
