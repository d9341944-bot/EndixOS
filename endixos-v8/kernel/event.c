/* Кольцевая очередь событий ввода между IRQ и главным циклом. */
#include "event.h"
#include "io.h"

#define QSIZE 256                      /* степень двойки */

static event_t queue[QSIZE];
static volatile u32 head, tail;        /* head - куда писать, tail - откуда читать */

void event_push(int type, i32 a, i32 b, i32 c)
{
    u32 flags = irq_save();

    /* Подряд идущие движения мыши с теми же кнопками склеиваем: важна лишь последняя позиция. */
    if (type == EV_MOUSE && head != tail) {
        event_t *last = &queue[(head - 1) & (QSIZE - 1)];
        if (last->type == EV_MOUSE && last->c == c) {
            last->a = a;
            last->b = b;
            irq_restore(flags);
            return;
        }
    }

    if (((head + 1) & (QSIZE - 1)) != tail) {      /* при переполнении новые события теряем */
        queue[head].type = type;
        queue[head].a = a;
        queue[head].b = b;
        queue[head].c = c;
        head = (head + 1) & (QSIZE - 1);
    }
    irq_restore(flags);
}

int event_pending(void)
{
    return head != tail;
}

int event_pop(event_t *out)
{
    u32 flags = irq_save();
    if (head == tail) {
        irq_restore(flags);
        return 0;
    }
    *out = queue[tail];
    tail = (tail + 1) & (QSIZE - 1);
    irq_restore(flags);
    return 1;
}
