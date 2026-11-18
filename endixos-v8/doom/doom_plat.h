#ifndef DOOM_PLAT_H
#define DOOM_PLAT_H
#include <stdint.h>
#define DOOM_W 320
#define DOOM_H 200
/* Оригинальный Doom (движок id Software через doomgeneric). WAD ищет doom_disk.c. */
int  doom_start(const void *wad, uint32_t wadsize, const char *wadname);   /* 0 - готово, иначе ошибка: doom_error() */
void doom_frame(void);                 /* один кадр движка; после выхода doom_alive() == 0 */
int  doom_alive(void);
const uint32_t *doom_screen(void);     /* 320x200, 0x00RRGGBB */
const char *doom_error(void);
void doom_stop(void);
void doom_input(int sc, int ext, int press);   /* скан-коды набора 1 */
void doom_release_all(void);
void doom_mouse(int dx, int dy, int btn);    /* относительная мышь (можно из прерывания) */
void doom_load_save(const char *name, const void *data, uint32_t size);   /* вернуть сохранение из диска */
void *doom_alloc_tmp(uint32_t size);
void doom_free_tmp(void *p);
void *doom_alloc_wad(uint32_t size);   /* место под WAD в арене (до doom_start) */
uint32_t doom_wad_max(void);
/* реализует ядро / тест */
uint32_t doom_now_ms(void);
void doom_idle(void);
#endif
