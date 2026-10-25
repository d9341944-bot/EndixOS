#ifndef ENDIX_EVENT_H
#define ENDIX_EVENT_H

#include "types.h"

#define EV_MOUSE 1   /* a = x, b = y, c = кнопки (бит0 левая, бит1 правая, бит2 средняя) */
#define EV_KEY   2   /* a = символ (ASCII, '\b', '\n', '\t', 27) или KEY_*, b = 1 если нажат Ctrl */

/* Коды клавиш без символа (значения выше ASCII). */
#define KEY_UP     0x101
#define KEY_DOWN   0x102
#define KEY_LEFT   0x103
#define KEY_RIGHT  0x104
#define KEY_DELETE 0x105
#define KEY_F2     0x106

typedef struct {
    int type;
    i32 a, b, c;
} event_t;

/* push вызывается из обработчиков IRQ, pop - из главного цикла. */
void event_push(int type, i32 a, i32 b, i32 c);
int  event_pop(event_t *out);
int  event_pending(void);      /* 1, если очередь не пуста */

#endif
