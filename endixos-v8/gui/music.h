#ifndef ENDIX_MUSIC_H
#define ENDIX_MUSIC_H
#include "../kernel/types.h"
#define MUS_W 400
#define MUS_H 480
/* Плеер: три встроенные мелодии, WAV (PCM 8/16 бит, моно/стерео, любая частота) из ~/Music и с диска, папка /Music.
 * Звук идёт через драйвер Intel HD Audio (hda_play_buf). Трек целиком декодируется в память: до 60 секунд. */
void music_init(void);                          /* поиск треков (однократно; R в окне - заново) */
int  music_key(i32 key);                        /* 1 - перерисовать */
int  music_click(i32 x, i32 y);
int  music_wheel(int d);
int  music_tick(u32 ticks, int visible);        /* продвигает время и переключает трек; 1 - перерисовать */
void music_draw(i32 x, i32 y, i32 w, i32 h);
int  music_playing(void);
const char *music_now(int *pct, const char **sub);   /* название выбранного трека ("" - нет) и процент проигранного */
#endif
