#ifndef ENDIX_PERF_H
#define ENDIX_PERF_H
#include "../kernel/types.h"

/* Скорость интерфейса. Режимы: Auto (сам снижает качество, если кадры долгие), High (всегда полное), Balanced, Fast. */
enum { PERF_AUTO, PERF_HIGH, PERF_BALANCED, PERF_FAST, PERF_MODES };

void perf_init(u32 cycles_per_tick);     /* тактов процессора за один тик PIT (PIT_HZ = 100) */
void perf_frame(u32 cycles);             /* затраты на один отрисованный кадр сцены */
int  perf_mode(void);
void perf_set_mode(int m);
int  perf_level(void);                   /* 0..2 - какое качество сейчас применено */
int  perf_frame_ms(void);                /* сглаженное время кадра, мс (для проверки) */
#endif
