#ifndef ENDIX_UI_H
#define ENDIX_UI_H

#include "../kernel/types.h"

/* Общие элементы интерфейса в стиле macOS. */

enum { G_HOME, G_DESKTOP, G_DOC, G_DOWN, G_PHOTO, G_MUSIC, G_DISK, G_APPEAR, G_INFO };

void ui_button(i32 x, i32 y, i32 w, i32 h, const char *label, int enabled, int primary);
void ui_chevron(i32 cx, i32 cy, int dir, u32 color);          /* dir: -1 влево, +1 вправо */
void ui_switch(i32 x, i32 y, int on);                         /* 40x24 */
void ui_slider(i32 x, i32 y, i32 w, int v, int enabled);       /* 0..100, ручка 20 px, высота 24 */
void ui_segment(i32 x, i32 y, i32 w, const char *const *labels, int n, int sel);
void ui_group(i32 x, i32 y, i32 w, i32 h);                    /* скруглённая карточка настроек */
void ui_hline(i32 x, i32 y, i32 w);
void ui_glyph(int kind, i32 cx, i32 cy, u32 color, u32 cut);  /* значок ~14 px; cut - цвет "вырезов" */
void ui_squircle_icon(int kind, i32 x, i32 y, i32 size, u32 bg);   /* цветной квадрат со значком */

#endif
