#ifndef ENDIX_IMAGE_H
#define ENDIX_IMAGE_H

#include "../kernel/types.h"

/* Декодеры картинок: PNG (все типы цвета и глубины, без чересстрочной развёртки), JPEG (baseline и progressive),
 * GIF (первый кадр). Результат - пиксели 0x00RRGGBB, прозрачность смешана с белым фоном страницы.
 * Если ширина больше maxw, картинка уменьшается усреднением. Свой буфер не нужен: вся рабочая память статическая. */

#define IMAGE_MAXW 1280

/* out - место под cap пикселей. Возвращает 1 при успехе: w, h - размеры результата, nat_w, nat_h - размеры файла.
 * 0 - не удалось, *err - причина (статическая строка). */
int image_decode(const u8 *data, u32 n, u32 maxw, u32 *out, u32 cap, u32 *w, u32 *h, u32 *nat_w, u32 *nat_h, const char **err);

/* Размеры по заголовку без декодирования (1 - узнали). */
int image_probe(const u8 *data, u32 n, u32 *w, u32 *h);

#endif
