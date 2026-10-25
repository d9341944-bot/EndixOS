/* Логотип EndixOS: 4 круга по углам + крест между ними. Рисуется со сглаживанием 3x3. */
#include "logo.h"
#include "fb.h"

#define SS 3

/* Точка в единицах 1/(2*N): I, J - координаты удвоенные (нечётные). N2 = 2*size*SS. */
static int inside(i32 I, i32 J, i32 N2)
{
    i32 q = N2 / 4, r = q;
    i32 cx[2] = { q, 3 * q };
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++) {
            i32 dx = I - cx[a], dy = J - cx[b];
            if (dx * dx + dy * dy <= r * r)
                return 1;
        }
    i32 lo = N2 * 73 / 1000, hi = N2 - lo;             /* глубина выемки между лепестками */
    if (I >= q && I <= 3 * q && J >= lo && J <= hi) return 1;
    if (J >= q && J <= 3 * q && I >= lo && I <= hi) return 1;
    return 0;
}

void logo_draw(i32 x, i32 y, i32 size, u32 color)
{
    i32 N2 = 2 * size * SS;
    for (i32 py = 0; py < size; py++)
        for (i32 px = 0; px < size; px++) {
            u32 hit = 0;
            for (i32 sy = 0; sy < SS; sy++)
                for (i32 sx = 0; sx < SS; sx++)
                    hit += (u32)inside(2 * (px * SS + sx) + 1, 2 * (py * SS + sy) + 1, N2);
            if (hit)
                fb_blend_pixel(x + px, y + py, color, hit * 255 / (SS * SS));
        }
}
