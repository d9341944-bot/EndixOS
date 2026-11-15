#ifndef ENDIX_VIEWER_H
#define ENDIX_VIEWER_H
#include "../kernel/types.h"
/* Просмотр картинок из Files в отдельном окне: PNG, JPEG, GIF. Стрелки или кнопки - соседние картинки папки. */
int  viewer_is_image(int node);              /* по расширению: png, jpg, jpeg, gif */
int  viewer_open(int node);                  /* 0 - успех */
void viewer_draw(i32 x, i32 y, i32 w, i32 h);
int  viewer_key(i32 key);                    /* 1 - перерисовать */
int  viewer_click(i32 x, i32 y, i32 w);
const char *viewer_title(void);
#endif
