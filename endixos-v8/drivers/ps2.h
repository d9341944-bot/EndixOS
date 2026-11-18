#ifndef ENDIX_PS2_H
#define ENDIX_PS2_H

#include "../kernel/types.h"

/* Область, в которой живёт курсор (размер экрана). Курсор ставится в центр. */
void ps2_set_bounds(u32 width, u32 height);

/* Инициализация контроллера 8042, клавиатуры и мыши. Вызывать до sti. */
void ps2_init(void);

/* Тачпад Synaptics (если найден): абсолютный режим, свои жесты. Иначе обычная PS/2-мышь. НЕ проверено на железе. */
int  ps2_tp_present(void);
void ps2_tp_set(int speed, int tap, int natural);        /* скорость 0..100, касание = щелчок, естественная прокрутка */
void ps2_tp_get(int *speed, int *tap, int *natural);
void ps2_tp_diag(char *out, int cap);                    /* текст для команды touchpad */

/* Раскладка клавиатуры: 0 = EN (US), 1 = RU (ЙЦУКЕН). Alt+Shift или Win+Пробел переключают. */
int  kbd_layout(void);
void kbd_set_layout(int ru);

/* Сырые коды клавиш (набор 1, ext = префикс E0) для игр. Пока hook задан и возвращает 1, обычные события клавиш не создаются. */
extern int (*ps2_raw_hook)(u8 sc, int ext, int press);
extern int (*ps2_mouse_hook)(int dx, int dy, int btn);   /* dy: вниз - плюс */

#endif
