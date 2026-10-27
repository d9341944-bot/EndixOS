#ifndef ENDIX_FILES_H
#define ENDIX_FILES_H

#include "../kernel/types.h"

/* Размер области содержимого окна Files (под заголовком). */
#define FILES_W 720
#define FILES_H 416

void files_set_size(i32 w, i32 h);                 /* размер области содержимого окна */
void files_new_folder(void);                       /* меню File: New Folder */
void files_goto(int node);                         /* показать узел: папку открыть, файл выбрать в его папке */
void files_init(void);                                /* открыть домашнюю папку */

/* x, y - левый верхний угол области содержимого. */
void files_draw(i32 x, i32 y);

/* Клик левой кнопкой. rx, ry - от угла области содержимого, ticks - pit_ticks()
 * (для двойного клика). 1 - нужна перерисовка. */
int  files_click(i32 rx, i32 ry, u32 ticks);

/* Клавиша: стрелки, Enter (открыть), F2 (переименовать), Delete, Backspace (наверх). */
int  files_key(i32 key);

#endif
