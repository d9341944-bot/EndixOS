#include "settings.h"
#include "wm.h"
#include "framebuffer.h"
#include "fat16.h"
#include "keyboard.h"
#include "tty.h"
#include "pmm.h"
#include <stdint.h>

extern volatile uint32_t ticks;
struct os_settings g_settings;

static int settings_win_id = -1;
static int tab_sel = 0;
static int wall_col  = 0;   /* 0=static, 1=animated */
static int theme_col = 0;   /* 0=static, 1=extra */
#define TAB_COUNT 5
static const char* tabs[TAB_COUNT] = { "System", "Display", "Wall", "Theme", "About" };

/* ================= 10 ТЕМ ================= */
#define THEME_COUNT 10

struct ui_theme {
    uint32_t bg, surface, surface2, surface3;
    uint32_t primary, primary_dim;
    uint32_t on_surface, on_variant, outline;
    const char* name;
};

static const struct ui_theme ui_themes[THEME_COUNT] = {
    /* 0 Adwaita Dark */
    { 0x1E1E1E, 0x242424, 0x2E2E2E, 0x3A3A3A,
      0x3584E4, 0x1C4A7A, 0xFFFFFF, 0xB0B0B0, 0x3A3A3A, "Adwaita Dark" },
    /* 1 Adwaita Light */
    { 0xFAFAFA, 0xFFFFFF, 0xF0F0F0, 0xE0E0E0,
      0x3584E4, 0xB0D0F0, 0x1A1A1A, 0x606060, 0xD0D0D0, "Adwaita Light" },
    /* 2 Tokyo Night */
    { 0x1A1B26, 0x16161E, 0x1F2335, 0x292E42,
      0x7AA2F7, 0x2A3655, 0xC0CAF5, 0x9AA5CE, 0x3B4261, "Tokyo Night" },
    /* 3 Catppuccin Mocha */
    { 0x1E1E2E, 0x181825, 0x313244, 0x45475A,
      0xCBA6F7, 0x4E3D5E, 0xCDD6F4, 0xBAC2DE, 0x585B70, "Catppuccin" },
    /* 4 Dracula */
    { 0x282A36, 0x21222C, 0x44475A, 0x6272A4,
      0xBD93F9, 0x4A3A6A, 0xF8F8F2, 0xBD93F9, 0x6272A4, "Dracula" },
    /* 5 Nord */
    { 0x2E3440, 0x2B303B, 0x3B4252, 0x434C5E,
      0x88C0D0, 0x3B5258, 0xECEFF4, 0xD8DEE9, 0x4C566A, "Nord" },
    /* 6 Gruvbox */
    { 0x1D2021, 0x282828, 0x3C3836, 0x504945,
      0xFABD2F, 0x5A4A1E, 0xEBDBB2, 0xA89984, 0x665C54, "Gruvbox" },
    /* 7 Rosé Pine */
    { 0x191724, 0x1F1D2E, 0x26233A, 0x403D52,
      0xEBBCBA, 0x5A3D44, 0xE0DEF4, 0x908CAA, 0x524F67, "Rosé Pine" },
    /* 8 Synthwave */
    { 0x241B2F, 0x1A1228, 0x2E1F44, 0x402E5C,
      0xFF7EDB, 0x6A2A5A, 0xFFEEFF, 0xC090B0, 0x5A3A6A, "Synthwave" },
    /* 9 Solarized */
    { 0x002B36, 0x073642, 0x0A4A55, 0x186C7A,
      0x268BD2, 0x18507A, 0xFDF6E3, 0x93A1A1, 0x586E75, "Solarized" },
};

void settings_apply_theme(int id) {
    if (id < 0 || id >= THEME_COUNT) id = 0;
    g_settings.theme_id    = id;
    g_settings.bg          = ui_themes[id].bg;
    g_settings.surface     = ui_themes[id].surface;
    g_settings.surface2    = ui_themes[id].surface2;
    g_settings.surface3    = ui_themes[id].surface3;
    g_settings.primary     = ui_themes[id].primary;
    g_settings.primary_dim = ui_themes[id].primary_dim;
    g_settings.on_surface  = ui_themes[id].on_surface;
    g_settings.on_variant  = ui_themes[id].on_variant;
    g_settings.outline     = ui_themes[id].outline;
}

/* ================= 10 ОБОЕВ ================= */
#define WALL_COUNT 10
static const char* wall_names[WALL_COUNT] = {
    "Sunset", "Ocean", "Forest", "Mountains", "Aurora",
    "Night", "Desert", "City", "Waves", "Geo"
};

/* Псевдослучайные */
static uint32_t rng = 1234567;
static uint32_t rnd(void) { rng = rng * 1103515245 + 12345; return rng; }

/* Хелпер — залить треугольник снизу (горы/деревья) */
static void draw_peak(int cx, int base_y, int width, int height, uint32_t color) {
    for (int y = 0; y < height; y++) {
        int half = (width * y) / height / 2;
        int yy = base_y - y;
        if (yy < 0) continue;
        for (int x = cx - half; x <= cx + half; x++) {
            if (x >= 0 && x < (int)fb_width()) fb_putpixel(x, yy, color);
        }
    }
}

/* Хелпер — круг */
static void draw_circle_fill(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                fb_putpixel(cx + x, cy + y, color);
}

void settings_draw_wallpaper(void) {
    int W = (int)fb_width();
    int H = (int)fb_height();
    int theme = g_settings.wallpaper_id;
    if (theme < 0 || theme >= WALL_COUNT) theme = 0;
    rng = 1234 + theme * 9999;

    /* ============ 0. SUNSET ============ */
    if (theme == 0) {
        /* Небо — градиент розово-оранжевый */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (255*(255-t) + 80*t) / 255;
            uint32_t g = (100*(255-t) + 30*t) / 255;
            uint32_t b = (140*(255-t) + 60*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Солнце */
        draw_circle_fill(W*2/3, H*2/3, 90, 0xFFE0A0);
        draw_circle_fill(W*2/3, H*2/3, 70, 0xFFFFD0);
        /* Силуэт пальм */
        for (int i = 0; i < 3; i++) {
            int px = 100 + i * 200;
            for (int y = 0; y < 200; y++)
                for (int dx = -3; dx <= 3; dx++)
                    fb_putpixel(px + dx, H - 60 - y, 0x1A0A10);
            /* Листья */
            for (int a = 0; a < 6; a++) {
                int lx = px + (a - 3) * 15;
                int ly = H - 260 - (a % 2) * 20;
                for (int r = 0; r < 30; r++)
                    for (int d = -5; d <= 5; d++)
                        fb_putpixel(lx + (d + a * 2) * r / 8, ly + r, 0x1A0A10);
            }
        }
        /* Вода */
        for (int y = H - 60; y < H; y++)
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, 0x200808);
    }

    /* ============ 1. OCEAN ============ */
    else if (theme == 1) {
        /* Небо */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (100*(255-t) + 30*t) / 255;
            uint32_t g = (170*(255-t) + 60*t) / 255;
            uint32_t b = (230*(255-t) + 120*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Солнце */
        draw_circle_fill(W/4, 80, 50, 0xFFFFD0);
        /* Волны — 8, двигаются влево */
        int off = (ticks / 3) % 40;   /* смещение */
        for (int wave = 0; wave < 8; wave++) {
            int base_y = H/3 + wave * 40;
            uint32_t c = 0x0A3050 + wave * 0x081020;
            for (int x = 0; x < W; x++) {
                int xx = (x + off + wave * 10) % W;
                int wave_y = base_y + (xx * 2) % 20 - 10 + ((xx*xx)/100) % 8;
                for (int dy = 0; dy < 4; dy++)
                    if (wave_y+dy < H) fb_putpixel(x, wave_y+dy, c);
            }
        }
    }

    /* ============ 2. FOREST ============ */
    else if (theme == 2) {
        /* Небо с облаками */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (120*(255-t) + 30*t) / 255;
            uint32_t g = (180*(255-t) + 70*t) / 255;
            uint32_t b = (200*(255-t) + 50*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Солнце */
        draw_circle_fill(200, 100, 60, 0xFFF0A0);
        /* Задний ряд деревьев */
        for (int i = 0; i < 8; i++) {
            int px = i * (W/7) + 60;
            int ph = 200 + rnd() % 100;
            int pw = 60 + rnd() % 40;
            draw_peak(px, H - 20, pw, ph, 0x1A4020);
        }
        /* Передний ряд */
        for (int i = 0; i < 5; i++) {
            int px = i * (W/4) + 100;
            int ph = 250 + rnd() % 100;
            int pw = 90 + rnd() % 50;
            draw_peak(px, H, pw, ph, 0x0E2810);
            /* Ствол */
            for (int y = 0; y < 30; y++)
                for (int dx = -5; dx <= 5; dx++)
                    fb_putpixel(px + dx, H - 1 - y, 0x301810);
        }
        /* Земля */
        for (int y = H - 60; y < H; y++)
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, 0x0A1A0E);
    }

    /* ============ 3. MOUNTAINS ============ */
    else if (theme == 3) {
        /* Небо */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (180*(255-t) + 40*t) / 255;
            uint32_t g = (200*(255-t) + 50*t) / 255;
            uint32_t b = (220*(255-t) + 80*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Солнце за горами */
        draw_circle_fill(W - 200, 180, 70, 0xFFF8C0);
        /* Снежные шапки — задние */
        draw_peak(200, H-100, 500, 400, 0x404850);
        draw_peak(W-200, H-100, 600, 500, 0x505860);
        draw_peak(W/2, H-50, 700, 350, 0x404850);
        /* Передние — тёмные */
        draw_peak(100, H, 500, 350, 0x282838);
        draw_peak(W-100, H, 600, 450, 0x303040);
        /* Снег на шапках */
        for (int i = 0; i < 30; i++) {
            int sx = 100 + rnd() % (W-200);
            int sy = 200 + rnd() % 300;
            draw_circle_fill(sx, sy, 2 + rnd()%3, 0xFFFFFF);
        }
    }

    /* ============ 4. AURORA ============ */
    else if (theme == 4) {
        /* Ночное небо */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (10*(255-t) + 20*t) / 255;
            uint32_t g = (15*(255-t) + 40*t) / 255;
            uint32_t b = (40*(255-t) + 60*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Звёзды */
        for (int i = 0; i < 200; i++) {
            int sx = rnd() % W, sy = rnd() % (H/2);
            fb_putpixel(sx, sy, 0xFFFFFF);
        }
        /* Aurora — волны зелёно-фиолетовые, живые */
        uint32_t colors[4] = { 0x40FF80, 0x40C0FF, 0xA060FF, 0x60FFC0 };
        int off = (ticks / 4) % 100;
        for (int band = 0; band < 4; band++) {
            int y0 = H/4 + band * 60;
            for (int x = 0; x < W; x++) {
                int xx = (x + off * (band + 1)) % W;
                int wave = y0 + ((xx*3)%60) + ((xx*xx)/300)%40;
                for (int dy = -30; dy <= 30; dy++) {
                    int y = wave + dy;
                    if (y < 0 || y >= H) continue;
                    int a = 100 - (dy<0?-dy:dy)*3;
                    if (a <= 0) continue;
                    uint32_t c = fb_getpixel(x, y);
                    uint32_t cr = (c>>16)&0xFF, cg = (c>>8)&0xFF, cb = c&0xFF;
                    uint32_t nr = (colors[band]>>16)&0xFF;
                    uint32_t ng = (colors[band]>>8)&0xFF;
                    uint32_t nb = colors[band]&0xFF;
                    cr = (cr*(255-a) + nr*a) / 255;
                    cg = (cg*(255-a) + ng*a) / 255;
                    cb = (cb*(255-a) + nb*a) / 255;
                    fb_putpixel(x, y, (cr<<16)|(cg<<8)|cb);
                }
            }
        }
    }

    /* ============ 5. NIGHT ============ */
    else if (theme == 5) {
        /* Тёмное небо */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (5*(255-t) + 15*t) / 255;
            uint32_t g = (5*(255-t) + 15*t) / 255;
            uint32_t b = (30*(255-t) + 50*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Луна */
        draw_circle_fill(W - 200, 150, 80, 0xFFFFF0);
        draw_circle_fill(W - 200, 150, 60, 0xFFF8D0);
        /* Кратеры */
        draw_circle_fill(W-190, 140, 10, 0xE0E0C0);
        draw_circle_fill(W-220, 170, 15, 0xD0D0B0);
        draw_circle_fill(W-180, 180, 8, 0xE0E0C0);
        /* Звёзды — ярче */
        for (int i = 0; i < 400; i++) {
            int sx = rnd() % W, sy = rnd() % H;
            int bright = 150 + rnd() % 105;
            fb_putpixel(sx, sy, (bright<<16)|(bright<<8)|bright);
        }
        /* Особо яркие */
        for (int i = 0; i < 20; i++) {
            int sx = rnd() % W, sy = rnd() % (H/2);
            draw_circle_fill(sx, sy, 1, 0xFFFFFF);
        }
    }

    /* ============ 6. DESERT ============ */
    else if (theme == 6) {
        /* Небо */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (255*(255-t) + 200*t) / 255;
            uint32_t g = (220*(255-t) + 150*t) / 255;
            uint32_t b = (150*(255-t) + 80*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Большое солнце */
        draw_circle_fill(W/2, H/2, 120, 0xFFE080);
        /* Дюны */
        for (int x = 0; x < W; x++) {
            int dune = H*2/3 + (x*3)%80 - 40;
            for (int y = dune; y < H; y++) {
                uint32_t t = ((y-dune) * 255) / (H-dune+1);
                uint32_t r = (240*(255-t) + 160*t) / 255;
                uint32_t g = (200*(255-t) + 120*t) / 255;
                uint32_t b = (140*(255-t) + 80*t) / 255;
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
            }
        }
        /* Кактусы */
        for (int i = 0; i < 3; i++) {
            int cx = 150 + i * 300;
            int cy = H - 100;
            for (int y = 0; y < 80; y++)
                for (int dx = -8; dx <= 8; dx++)
                    fb_putpixel(cx + dx, cy - y, 0x204020);
            for (int y = 0; y < 40; y++)
                for (int dx = -8; dx <= 0; dx++)
                    fb_putpixel(cx - 25 + dx, cy - 40 - y, 0x204020);
            for (int y = 0; y < 40; y++)
                for (int dx = 0; dx <= 8; dx++)
                    fb_putpixel(cx + 25 + dx, cy - 30 - y, 0x204020);
        }
    }

    /* ============ 7. CITY ============ */
    else if (theme == 7) {
        /* Ночное небо */
        for (int y = 0; y < H; y++) {
            uint32_t t = (y * 255) / H;
            uint32_t r = (20*(255-t) + 40*t) / 255;
            uint32_t g = (20*(255-t) + 30*t) / 255;
            uint32_t b = (50*(255-t) + 80*t) / 255;
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, (r<<16)|(g<<8)|b);
        }
        /* Луна */
        draw_circle_fill(W - 150, 100, 40, 0xFFFFF0);
        /* Дома — задний ряд */
        for (int x = 0; x < W; x += 60) {
            int h = 150 + rnd() % 150;
            for (int y = 0; y < h; y++)
                for (int dx = 0; dx < 55; dx++)
                    fb_putpixel(x + dx, H - y, 0x101828);
        }
        /* Передний ряд */
        for (int x = 30; x < W; x += 80) {
            int h = 200 + rnd() % 200;
            for (int y = 0; y < h; y++)
                for (int dx = 0; dx < 70; dx++)
                    fb_putpixel(x + dx, H - y, 0x080C18);
            /* Окна светящиеся */
            for (int wy = 20; wy < h - 20; wy += 25)
                for (int wx = 10; wx < 60; wx += 20) {
                    if (rnd() % 3 != 0) {
                        uint32_t c = 0xFFFF80 + (rnd()%3)*0x202000;
                        for (int dy = 0; dy < 10; dy++)
                            for (int dx2 = 0; dx2 < 10; dx2++)
                                fb_putpixel(x + wx + dx2, H - wy - dy, c);
                    }
                }
        }
    }

    /* ============ 8. WAVES ============ */
    else if (theme == 8) {
        /* Фон — тёмный */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, 0x0A0A18);

        /* 5 синусоидальных волн разных цветов */
        uint32_t cols[5] = { 0xFF4080, 0xFF8040, 0xFFD040, 0x80FF40, 0x4080FF };
        for (int w = 0; w < 5; w++) {
            int base = H/2 + (w - 2) * 60;
            int amp  = 40 + w * 10;
            for (int x = 0; x < W; x++) {
                int y = base + (amp * ((x*x) % 200 - 100)) / 100;
                for (int dy = -3; dy <= 3; dy++)
                    if (y + dy >= 0 && y + dy < H)
                        fb_putpixel(x, y + dy, cols[w]);
            }
        }
    }

    /* ============ 9. GEO ============ */
    else if (theme == 9) {
        /* Тёмный фон */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                fb_putpixel(x, y, 0x1A1A2E);

        /* Сетка треугольников */
        uint32_t cols[3] = { 0x3A3A6E, 0x4A4A8E, 0x2A2A5E };
        int size = 100;
        for (int y = 0; y < H + size; y += size) {
            for (int x = 0; x < W + size; x += size) {
                uint32_t c = cols[(x/size + y/size) % 3];
                /* Треугольник */
                for (int t = 0; t < size; t++) {
                    for (int d = 0; d <= t; d++) {
                        if (y + t < H && x + d < W) fb_putpixel(x + d, y + t, c);
                        if (y + t < H && x + size - d - 1 < W && x + size - d - 1 >= 0)
                            fb_putpixel(x + size - d - 1, y + t, c);
                    }
                }
            }
        }
        /* Круги-акценты */
        for (int i = 0; i < 10; i++) {
            int cx = rnd() % W, cy = rnd() % H;
            int r = 20 + rnd() % 40;
            uint32_t c = 0xFF4080 + (rnd() % 4) * 0x2040;
            for (int y = -r; y <= r; y++)
                for (int x = -r; x <= r; x++) {
                    int d2 = x*x + y*y;
                    if (d2 <= r*r && d2 >= (r-4)*(r-4)) {
                        if (cx+x >= 0 && cx+x < W && cy+y >= 0 && cy+y < H)
                            fb_putpixel(cx + x, cy + y, c);
                    }
                }
        }
    }
}

/* ================= INIT/SAVE ================= */
void settings_init(void) {
    settings_apply_theme(0);
    g_settings.wallpaper_id = 0;
    g_settings.brightness = 100;
    struct fat16_dirent de;
    if (fat16_find("SETTINGS.CFG", &de) == 0 && de.size >= sizeof(g_settings)) {
        uint8_t buf[sizeof(g_settings)];
        fat16_read(&de, buf, sizeof(buf));
        for (uint32_t i = 0; i < sizeof(g_settings); i++)
            ((uint8_t*)&g_settings)[i] = buf[i];
    }
}

void settings_save(void) {
    fat16_delete("SETTINGS.CFG");
    if (fat16_create("SETTINGS.CFG") == 0)
        fat16_write("SETTINGS.CFG", (uint8_t*)&g_settings, sizeof(g_settings), 0);
}

/* ================= UI ================= */
static void itoa_u32(uint32_t v, char* out) {
    int n = 0;
    if (v == 0) out[n++] = '0';
    else { char t[12]; int m = 0;
        while (v > 0) { t[m++] = '0' + (v % 10); v /= 10; }
        while (m > 0) out[n++] = t[--m]; }
    out[n] = 0;
}

/* Хелпер: нарисовать один столбик списка обоев/тем */
static void draw_col_list(int x, int y, int w, int start, int count,
                          int sel_local, int active_global, int is_wall) {
    for (int i = 0; i < count; i++) {
        int iy = y + i * 26;
        int abs_id = start + i;
        int sel = (i == sel_local);
        int act = (abs_id == active_global);

        if (sel)
            gfx_rounded_fill(x + 4, iy - 2, w - 8, 24, 6, g_settings.surface3);

        /* Номер */
        char num[4];
        num[0] = '0' + ((abs_id+1) / 10);
        num[1] = '0' + ((abs_id+1) % 10);
        num[2] = 0;
        gfx_puts(x + 14, iy + 4, num,
                 sel ? g_settings.primary : g_settings.on_variant, 1);

        /* Имя — берём из wall_names[] или ui_themes[] */
        extern const char* wall_names_ext(int);
        extern const char* theme_names_ext(int);
        const char* name = is_wall ? wall_names_ext(abs_id)
                                    : theme_names_ext(abs_id);
        gfx_puts(x + 44, iy + 4, name,
                 sel ? g_settings.on_surface : g_settings.on_variant, 1);

        if (act)
            gfx_puts(x + w - 24, iy + 4, "*", g_settings.primary, 1);
    }
}

static void settings_draw(int x, int y, int w, int h, void* user) {
    (void)user;
    gfx_box_fill(x, y, w, h, g_settings.surface);

    /* Tabs */
    int tw = w / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        int tx = x + i * tw;
        uint32_t tc = (i == tab_sel) ? g_settings.primary : g_settings.surface2;
        uint32_t txt = (i == tab_sel) ? g_settings.bg : g_settings.on_variant;
        gfx_rounded_fill(tx + 4, y + 8, tw - 8, 26, 6, tc);
        int sl = 0; while (tabs[i][sl]) sl++;
        gfx_puts(tx + (tw - sl*8)/2, y + 14, tabs[i], txt, 1);
    }

    int yy = y + 56;

    if (tab_sel == 0) {   /* System */
        char buf[64];
        uint32_t total_kb = pmm_total_pages() * 4;
        uint32_t free_kb  = pmm_free_pages() * 4;
        uint32_t secs = ticks / 100;

        gfx_puts(x + 20, yy, "EndixOS", g_settings.on_surface, 2); yy += 36;

        itoa_u32(total_kb / 1024, buf);
        int n = 0; while (buf[n]) n++;
        buf[n]=' '; buf[n+1]='M'; buf[n+2]='B'; buf[n+3]=0;
        gfx_puts(x + 20, yy, "RAM:", g_settings.on_variant, 1);
        gfx_puts(x + 90, yy, buf, g_settings.on_surface, 1); yy += 22;

        itoa_u32(free_kb / 1024, buf);
        n = 0; while (buf[n]) n++;
        buf[n]=' '; buf[n+1]='M'; buf[n+2]='B'; buf[n+3]=0;
        gfx_puts(x + 20, yy, "Free:", g_settings.on_variant, 1);
        gfx_puts(x + 90, yy, buf, g_settings.on_surface, 1); yy += 22;

        itoa_u32(secs, buf);
        n = 0; while (buf[n]) n++;
        buf[n]='s'; buf[n+1]=0;
        gfx_puts(x + 20, yy, "Uptime:", g_settings.on_variant, 1);
        gfx_puts(x + 90, yy, buf, g_settings.on_surface, 1);
    }
    else if (tab_sel == 1) {   /* Display */
        gfx_puts(x + 20, yy, "Brightness", g_settings.on_surface, 1); yy += 30;
        int sx = x + 20, sy = yy, sw = 260;
        gfx_box_fill(sx, sy, sw, 6, g_settings.surface3);
        int fill = (sw * g_settings.brightness) / 100;
        gfx_box_fill(sx, sy, fill, 6, g_settings.primary);
        char buf[8]; itoa_u32(g_settings.brightness, buf);
        int n = 0; while (buf[n]) n++;
        buf[n]='%'; buf[n+1]=0;
        gfx_puts(sx + sw + 20, sy - 4, buf, g_settings.on_surface, 1);
    }
    else if (tab_sel == 2) {   /* WALL — два столбика */
        gfx_puts(x + 16, yy, "Static", g_settings.on_variant, 1);
        gfx_puts(x + w/2 + 10, yy, "Animated", g_settings.on_variant, 1);
        yy += 22;

        int cw = w/2 - 20;

        /* Столбик 1: статика, id 0-9 */
        int sel1 = (wall_col == 0) ? g_settings.wallpaper_id : -1;
        if (sel1 >= 10) sel1 = -1;
        draw_col_list(x + 8, yy, cw, 0, 10, sel1, g_settings.wallpaper_id, 1);

        /* Столбик 2: анимация, id 10-14 */
        int sel2 = (wall_col == 1) ? g_settings.wallpaper_id - 10 : -1;
        if (sel2 < 0 || sel2 > 4) sel2 = -1;
        draw_col_list(x + w/2 + 8, yy, cw, 10, 5, sel2, g_settings.wallpaper_id, 1);

        /* Разделитель */
        for (int py = yy; py < y + h - 24; py++)
            fb_putpixel(x + w/2, py, g_settings.outline);

        /* Подсказка */
        gfx_puts(x + 16, y + h - 22,
                 "LEFT/RIGHT switch   UP/DOWN select   ENTER save",
                 g_settings.on_variant, 1);
    }
    else if (tab_sel == 3) {   /* THEME — два столбика */
        gfx_puts(x + 16, yy, "Static", g_settings.on_variant, 1);
        gfx_puts(x + w/2 + 10, yy, "Extra", g_settings.on_variant, 1);
        yy += 22;

        int cw = w/2 - 20;

        int sel1 = (theme_col == 0) ? g_settings.theme_id : -1;
        if (sel1 >= 10) sel1 = -1;
        draw_col_list(x + 8, yy, cw, 0, 10, sel1, g_settings.theme_id, 0);

        int sel2 = (theme_col == 1) ? g_settings.theme_id - 10 : -1;
        if (sel2 < 0 || sel2 > 4) sel2 = -1;
        draw_col_list(x + w/2 + 8, yy, cw, 10, 5, sel2, g_settings.theme_id, 0);

        for (int py = yy; py < y + h - 24; py++)
            fb_putpixel(x + w/2, py, g_settings.outline);

        gfx_puts(x + 16, y + h - 22,
                 "LEFT/RIGHT switch   UP/DOWN select   ENTER save",
                 g_settings.on_variant, 1);
    }
    else {   /* About */
        gfx_puts(x + 20, yy, "EndixOS", g_settings.on_surface, 3); yy += 50;
        gfx_puts(x + 20, yy, "V5.0", g_settings.primary, 2); yy += 40;
        gfx_puts(x + 20, yy, "Operating system", g_settings.on_variant, 1); yy += 22;
        gfx_puts(x + 20, yy, "2026", g_settings.on_variant, 1);
    }
}



void settings_handle_key(int c) {
    if (settings_win_id < 0) return;

    /* Tab — всегда переключение вкладок */
    if (c == '\t') {
        tab_sel = (tab_sel + 1) % TAB_COUNT;
        wm_render();
        return;
    }

    /* ============ WALL (tab_sel == 2) ============ */
    if (tab_sel == 2) {
        if (c == KEY_LEFT)  { wall_col = 0; wm_render(); return; }
        if (c == KEY_RIGHT) { wall_col = 1; wm_render(); return; }

        if (wall_col == 0) {   /* статика 0-9 */
            if (c == KEY_UP)   { int id = g_settings.wallpaper_id - 1; if (id < 0) id = 9; g_settings.wallpaper_id = id; wm_render(); }
            if (c == KEY_DOWN) { int id = g_settings.wallpaper_id + 1; if (id > 9) id = 0; g_settings.wallpaper_id = id; wm_render(); }
        } else {                /* анимация 10-14 */
            if (c == KEY_UP)   { int id = g_settings.wallpaper_id - 1; if (id < 10) id = 14; g_settings.wallpaper_id = id; wm_render(); }
            if (c == KEY_DOWN) { int id = g_settings.wallpaper_id + 1; if (id > 14) id = 10; g_settings.wallpaper_id = id; wm_render(); }
        }
        if (c == '\n') settings_save();
        return;
    }

    /* ============ THEME (tab_sel == 3) ============ */
    if (tab_sel == 3) {
        if (c == KEY_LEFT)  { theme_col = 0; wm_render(); return; }
        if (c == KEY_RIGHT) { theme_col = 1; wm_render(); return; }

        if (theme_col == 0) {
            if (c == KEY_UP)   { int id = g_settings.theme_id - 1; if (id < 0) id = 9; settings_apply_theme(id); wm_render(); }
            if (c == KEY_DOWN) { int id = g_settings.theme_id + 1; if (id > 9) id = 0; settings_apply_theme(id); wm_render(); }
        } else {
            if (c == KEY_UP)   { int id = g_settings.theme_id - 1; if (id < 10) id = 14; settings_apply_theme(id); wm_render(); }
            if (c == KEY_DOWN) { int id = g_settings.theme_id + 1; if (id > 14) id = 10; settings_apply_theme(id); wm_render(); }
        }
        if (c == '\n') settings_save();
        return;
    }

    /* ============ Остальные вкладки: стрелки листают вкладки ============ */
    if (c == KEY_LEFT  && tab_sel > 0)              { tab_sel--; wm_render(); return; }
    if (c == KEY_RIGHT && tab_sel < TAB_COUNT - 1)  { tab_sel++; wm_render(); return; }

    if (tab_sel == 1) {   /* Display */
        if (c == KEY_UP)   { g_settings.brightness += 10; if (g_settings.brightness > 100) g_settings.brightness = 100; wm_render(); }
        if (c == KEY_DOWN) { g_settings.brightness -= 10; if (g_settings.brightness < 0)   g_settings.brightness = 0;   wm_render(); }
    }
    if (c == '\n') settings_save();
}



void settings_open(void) {
    if (settings_win_id >= 0 && wm_is_used(settings_win_id)) {
        wm_raise(settings_win_id); return;
    }
    settings_win_id = wm_create("Settings", 180, 60, 620, 520, settings_draw, 0);
    tab_sel = 0;
    wm_render();
}

/* Проверка: анимированные ли обои (id 10-14) */
int settings_wallpaper_is_animated(void) {
    return g_settings.wallpaper_id >= 10;
}

/* Проверка: анимированные ли обои (id 10-14) */
const char* wall_names_ext(int id) {
    if (id < 0 || id >= 15) return "?";
    return wall_names[id];
}
const char* theme_names_ext(int id) {
    if (id < 0 || id >= 15) return "?";
    return ui_themes[id].name;
}
