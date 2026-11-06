#ifndef ENDIX_IMG_H
#define ENDIX_IMG_H

#include "../kernel/types.h"

/* Декодеры картинок: PNG (без чересстрочности), JPEG (baseline, без прогрессивных), GIF (первый кадр).
 * Результат - пиксели 0x00RRGGBB, прозрачность смешана с белым фоном страницы. Без float, без malloc. */

enum { IMG_NONE, IMG_PNG, IMG_JPEG, IMG_GIF };

#define IMG_MAX_PIXELS (2 * 1000 * 1000)       /* больше - отказ (экономим память и время) */

/* Определяет формат и размер. Возвращает IMG_*; 0 - не картинка или формат не поддержан. */
int img_info(const u8 *d, u32 n, int *w, int *h);

/* Декодирует с уменьшением до tw x th (tw <= w, th <= h; равные размеры - без уменьшения).
 * out - tw*th слов. 1 - успех. */
int img_decode(const u8 *d, u32 n, int tw, int th, u32 *out);
/* То же, но с другим пределом числа пикселей исходной картинки (для обоев-фотографий). */
int img_decode_ex(const u8 *d, u32 n, int tw, int th, u32 *out, u32 maxpix);

#endif
