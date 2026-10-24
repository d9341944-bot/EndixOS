#ifndef ENDIX_THEME_H
#define ENDIX_THEME_H

#include "../kernel/types.h"

typedef struct {
    u32 body;               /* фон окна */
    u32 title_f, title_u;   /* заголовок: в фокусе / без фокуса */
    u32 ttext_f, ttext_u;   /* текст заголовка */
    u32 text, sub;          /* основной и приглушённый текст */
    u32 side;               /* боковая панель */
    u32 card;               /* карточки */
    u32 mbar_tint, mbar_ink;
    u32 dock_tint;
    u8  mbar_alpha, dock_alpha, line_alpha;
} theme_t;

#define ACCENT_COUNT 6

const theme_t *theme(void);                  /* текущая тема */
const theme_t *theme_variant(int dark);      /* конкретный вариант (для терминала) */
int  theme_is_dark(void);
void theme_set_dark(int dark);

u32  theme_accent(void);
int  theme_accent_index(void);
void theme_set_accent(int idx);
u32  theme_accent_color(int idx);
const char *theme_accent_name(int idx);

#endif
