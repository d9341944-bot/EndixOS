#ifndef ENDIX_HTML_H
#define ENDIX_HTML_H

#include "../kernel/types.h"

/* Разбор и вёрстка простого HTML (без CSS и скриптов): заголовки, абзацы, списки, ссылки, pre, цитаты, hr,
 * таблицы как строки текста. Страница загружается один раз, а раскладывается под любую ширину. */

enum { BK_TEXT, BK_HR, BK_DOT, BK_BG, BK_BAR, BK_IMG, BK_NONE };           /* BK_DOT - маркер списка, BK_BG - фон pre, BK_BAR - линия цитаты */
enum { HC_TEXT, HC_LINK, HC_GRAY, HC_HEAD };               /* роль цвета, настоящий цвет выбирает рисующий */

typedef struct {
    i32 x, y;                  /* от левого верхнего угла страницы */
    i32 w, h;
    const char *text;          /* не заканчивается нулём: длина в len */
    u16 len;
    u8  kind, font, color, uline;
    i16 link;                  /* номер ссылки или -1 */
    u32 rgb;                   /* бит 24 - свой цвет: текст или фон (0x1RRGGBB) */
} hbox_t;

void html_load(const char *src, u32 len, const char *base_url, int plain);   /* разобрать (старое содержимое отбрасывается); plain - обычный текст */
void html_layout(i32 width);                                      /* разложить под ширину области страницы */
i32  html_height(void);
int  html_box_count(void);
const hbox_t *html_box(int i);
const char *html_title(void);                                     /* "" если нет */
const char *html_link_url(int id);                                /* абсолютный адрес или 0 */
int  html_link_at(i32 x, i32 y);                                  /* номер ссылки под точкой страницы или -1 */
int  html_image_count(void);                                      /* картинки страницы (до 16) */
const char *html_image_url(int i);
int  html_image_state(int i);                                     /* 0 ждёт, 1 готова, 2 не вышло */
void html_image_set(int i, int nw, int nh);                       /* настоящий размер; nw <= 0 - не вышло. Потом нужен html_layout */
u32  html_page_bg(void);                                          /* цвет фона страницы 0xRRGGBB */
int  html_truncated(void);                                        /* 1, если страница слишком велика и обрезана */

#endif
