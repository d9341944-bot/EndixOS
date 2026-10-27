#ifndef ENDIX_FB_H
#define ENDIX_FB_H

#include "../kernel/types.h"

#define FB_MAX_W 1280
#define FB_MAX_H 1024

/* Цвет: 0x00RRGGBB */
#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

int  fb_init(u32 addr, u32 pitch, u32 width, u32 height, u32 bpp);
u32  fb_width(void);
u32  fb_height(void);

void fb_put_pixel(i32 x, i32 y, u32 color);
void fb_blend_pixel(i32 x, i32 y, u32 color, u32 alpha);   /* alpha 0..255 */
void fb_fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void fb_blend_rect(i32 x, i32 y, i32 w, i32 h, u32 color, u32 alpha);
void fb_fill_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color, u32 alpha);
void fb_fill_circle(i32 cx, i32 cy, i32 r, u32 color);

/* Прямой доступ к теневому буферу (шаг строки = fb_width()) и копирование готовых пикселей. */
u32 *fb_buffer(void);
void fb_blit(i32 x, i32 y, const u32 *src, i32 w, i32 h);

/* Кэш статичных обоев: один раз рисуем, потом каждый кадр копируем одним махом. */
void fb_save_background(void);
void fb_restore_background(void);

/* Двойная буферизация: рисуем в теневой буфер, потом копируем на экран. */
void fb_flip(void);                      /* выводит кадр, записывая на экран только изменения */

/* Курсор рисуется прямо в видеопамять после fb_flip и двигается без перерисовки сцены. */
void fb_frame_put_pixel(i32 x, i32 y, u32 color);
void fb_frame_restore(i32 x, i32 y, i32 w, i32 h);
u32  fb_flipped_bytes(void);

/* Liquid glass: размытие фона, тонировка (tint, ta 0..255), блик sheen и световая кромка.
 * corners: какие углы скруглять (1 лв, 2 пв, 4 лн, 8 пн; 15 - все). br - радиус размытия (0 - без размытия). */
#define GLASS_ALL 15
void fb_shadow(i32 x, i32 y, i32 w, i32 h, i32 dy, i32 spread, u32 amax);   /* тень окна (чёрная, amax 0..255) */
enum { GLASS_REFRACT, GLASS_BLUR, GLASS_FROST, GLASS_EDGE, GLASS_VIVID, GLASS_PARAMS };
void fb_glass_param_set(int id, int v);   /* настройки Liquid Glass, 0..100 */
int  fb_glass_param_get(int id);
void fb_glass_param_reset(void);
void fb_glass_set_reduced(int on);        /* без размытия и плотнее - быстрее и контрастнее */
void fb_glass(i32 x, i32 y, i32 w, i32 h, i32 r, u32 corners, i32 br, u32 tint, u32 ta, u32 sheen);             /* сколько байт записано в видеопамять всего */

#endif
