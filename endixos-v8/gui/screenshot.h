#ifndef ENDIX_SHOT_H
#define ENDIX_SHOT_H
#include "../kernel/types.h"
/* Снимок экрана: PNG (несжатые deflate-блоки) в /Pictures на диске. Возвращает 0 и имя файла в name, иначе текст ошибки в name. */
int screenshot_take(char *name, int cap);
#endif
