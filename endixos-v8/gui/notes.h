#ifndef ENDIX_NOTES_H
#define ENDIX_NOTES_H
#include "../kernel/types.h"
/* Два редактора на одном движке: Notes (одна заметка) и TextEdit (любой текстовый файл из Files). */
void notes_init(void);
const char *notes_preview(void);              /* текст заметки для виджета */

int  notes_key(i32 key, int ctrl);           /* 1 - нужна перерисовка */
int  notes_click(i32 x, i32 y);              /* координаты относительно содержимого окна */
int  notes_tick(u32 ticks, int visible);     /* автосохранение и мигание курсора; 1 - перерисовать */
void notes_draw(i32 x, i32 y, i32 w, i32 h);

int  te_open(int node);                      /* открыть файл в TextEdit (предыдущий сохраняется); 0 - успех */
int  te_key(i32 key, int ctrl);              /* Ctrl+S - сохранить сразу */
int  te_click(i32 x, i32 y);
int  te_tick(u32 ticks, int visible);
void te_draw(i32 x, i32 y, i32 w, i32 h);
const char *te_title(void);
int  te_is_text(int node);                   /* файл можно открыть как текст */
#endif
