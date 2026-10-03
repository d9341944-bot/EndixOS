#include "cmatrix.h"
#include "framebuffer.h"
#include "keyboard.h"
#include <stdint.h>

extern volatile uint32_t ticks;

/* Быстрая заливка чёрным одного символа-клетки */
static void clear_cell(int x, int y, int w, int h) {
    gfx_box_fill(x, y, w, h, 0x000000);
}

void run_cmatrix(void) {
    int W = (int)fb_width();
    int H = (int)fb_height();
    int CW = 8;      /* ширина клетки = ширина символа */
    int CH = 8;      /* высота клетки = высота символа */
    int cols = W / CW;
    int rows = H / CH;
    if (cols > 200) cols = 200;

    static int      col_y[200];
    static int      col_spd[200];
    static uint32_t col_rng[200];

    /* Инициализация столбцов */
    uint32_t seed = ticks * 2654435761u + 0xDEADBEEF;
    for (int i = 0; i < cols; i++) {
        seed = seed * 1103515245 + 12345;
        col_y[i]   = -(int)(seed % (rows * 2));
        seed = seed * 1103515245 + 12345;
        col_spd[i] = 1 + (seed % 2);
        col_rng[i] = seed;
    }

    /* Очистка экрана */
    gfx_clear(0x000000);
    gfx_flush();

    /* Мгновенный старт — рисуем всё сразу */
    while (1) {
        /* Проверяем ESC / Q на выход */
        while (keyboard_has_char()) {
            int c = keyboard_getchar();
            if (c == 27 || c == 'q' || c == 'Q') {
                gfx_clear(0x000000);
                gfx_flush();
                return;
            }
        }

        for (int i = 0; i < cols; i++) {
            int y = col_y[i];
            if (y < 0) { col_y[i] += col_spd[i]; continue; }

            /* Стираем самый старый символ в хвосте */
            int cy = y - 7;
            if (cy >= 0 && cy < rows)
                clear_cell(i*CW, cy*CH, CW, CH);

            /* Рисуем хвост из 7 символов с градиентом */
            for (int t = 0; t < 7; t++) {
                int ty = y - 6 + t;
                if (ty < 0 || ty >= rows) continue;

                uint32_t color;
                if      (t == 6) color = 0xE0FFE0;   /* голова — белый */
                else if (t == 5) color = 0x80FF80;
                else if (t == 4) color = 0x40E040;
                else if (t == 3) color = 0x20A020;
                else if (t == 2) color = 0x106010;
                else if (t == 1) color = 0x084008;
                else             color = 0x042004;

                /* Случайный символ */
                col_rng[i] = col_rng[i] * 1103515245 + 12345;
                uint8_t ch = 33 + (col_rng[i] % 94);   /* '!' .. '~' */

                clear_cell(i*CW, ty*CH, CW, CH);
                fb_putc((char)ch, i*CW, ty*CH, color, 1);
            }

            col_y[i] += col_spd[i];

            /* Столбец закончился — сбросить наверх */
            if (col_y[i] > rows + 7) {
                col_rng[i] = col_rng[i] * 1103515245 + 12345;
                col_y[i]   = -(int)(col_rng[i] % 40);
                col_rng[i] = col_rng[i] * 1103515245 + 12345;
                col_spd[i] = 1 + (col_rng[i] % 2);
            }
        }

        gfx_flush();

        /* Задержка между кадрами (~30 мс = ~33 FPS) */
        uint32_t st = ticks;
        while (ticks - st < 3) {
            for (volatile int i = 0; i < 5000; i++);
        }
    }
}
