#ifndef ENDIX_CLIP_H
#define ENDIX_CLIP_H
#include "../kernel/types.h"
/* Общий буфер обмена (только в RAM, на диск не пишется). Пока хранит одну картинку: снимок экрана,
 * уменьшенный до CLIP_MAXW x CLIP_MAXH. Вставляется в Заметки по Ctrl+V. */
#define CLIP_MAXW 440
#define CLIP_MAXH 280
void clip_copy_screen(void);                 /* скопировать текущий кадр */
int  clip_has_image(void);
int  clip_image_w(void);
int  clip_image_h(void);
const u32 *clip_image(void);                 /* пиксели 0x00RRGGBB, строка = clip_image_w() */
#endif
