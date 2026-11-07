#ifndef ENDIX_PS2_H
#define ENDIX_PS2_H

#include "../kernel/types.h"

/* Область, в которой живёт курсор (размер экрана). Курсор ставится в центр. */
void ps2_set_bounds(u32 width, u32 height);

/* Инициализация контроллера 8042, клавиатуры и мыши. Вызывать до sti. */
void ps2_init(void);

#endif
