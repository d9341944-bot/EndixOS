#ifndef ENDIX_SNAKE_H
#define ENDIX_SNAKE_H

#include "../kernel/types.h"

#define SNAKE_W    20
#define SNAKE_H    18
#define SNAKE_CELL 16

void snake_reset(void);

/* Нажатие клавиши (стрелки, WASD, пробел, Enter, R). 1 - нужна перерисовка. */
int  snake_key(i32 key);

/* Вызывать из главного цикла. ticks - pit_ticks(), active - окно сверху и видно.
 * Неактивная игра сама встаёт на паузу. 1 - нужна перерисовка. */
int  snake_tick(u32 ticks, int active);

/* Клик мышью по окну: запуск, продолжение или новая игра. */
int  snake_click(void);

/* x, y, w, h - область содержимого окна (под заголовком). */
void snake_draw(i32 x, i32 y, i32 w, i32 h);

#endif
