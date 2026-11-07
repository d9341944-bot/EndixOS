#ifndef ENDIX_HDA_H
#define ENDIX_HDA_H
#include "../kernel/types.h"

/* Intel High Definition Audio (контроллер PCI класса 0403 + кодек, например Realtek ALC269 нетбука).
 * Работа опросом, без прерываний: команды через CORB/RIRB, воспроизведение 48 кГц 16 бит стерео циклическим
 * буфером с остановкой по таймеру. Порядок инициализации по спецификации Intel HDA 1.0. НЕ проверено на железе. */
int  hda_init(void);                       /* 1 - контроллер и выход найдены, пути до динамиков настроены */
void hda_set_volume(int pct);              /* 0..100 */
void hda_play_chime(void);                 /* звук запуска */
void hda_play_pop(void);                   /* щелчок при смене громкости */
void hda_poll(u32 now_ticks, u32 hz);      /* останавливает воспроизведение, когда звук закончился */
int  hda_present(void);
int  hda_playing(void);
void hda_diag(char *out, int cap);         /* текст для команды audio */
#endif
