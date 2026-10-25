/* Светлая и тёмная темы и акцентные цвета. */
#include "theme.h"
#include "fb.h"

static const theme_t light = {
    .body = RGB(247, 247, 249),
    .title_f = RGB(232, 232, 237), .title_u = RGB(241, 241, 244),
    .ttext_f = RGB(55, 55, 62),    .ttext_u = RGB(150, 150, 156),
    .text = RGB(30, 30, 34),       .sub = RGB(120, 120, 128),
    .side = RGB(236, 236, 241),    .card = RGB(255, 255, 255),
    .mbar_tint = RGB(246, 246, 250), .mbar_ink = RGB(28, 28, 32),
    .dock_tint = RGB(235, 235, 245),
    .mbar_alpha = 215, .dock_alpha = 115, .line_alpha = 28,
};

static const theme_t dark = {
    .body = RGB(36, 36, 41),
    .title_f = RGB(62, 62, 70),    .title_u = RGB(48, 48, 54),
    .ttext_f = RGB(230, 230, 235), .ttext_u = RGB(140, 140, 148),
    .text = RGB(235, 235, 240),    .sub = RGB(150, 150, 158),
    .side = RGB(30, 30, 35),       .card = RGB(52, 52, 58),
    .mbar_tint = RGB(28, 28, 34),  .mbar_ink = RGB(235, 235, 240),
    .dock_tint = RGB(45, 45, 55),
    .mbar_alpha = 205, .dock_alpha = 120, .line_alpha = 90,
};

static const u32 accent_colors[ACCENT_COUNT] = {
    RGB(100, 70, 220),   /* Purple */
    RGB(10, 132, 255),   /* Blue */
    RGB(48, 190, 90),    /* Green */
    RGB(255, 149, 0),    /* Orange */
    RGB(255, 55, 120),   /* Pink */
    RGB(110, 115, 125),  /* Graphite */
};

static const char *const accent_names[ACCENT_COUNT] = {
    "Purple", "Blue", "Green", "Orange", "Pink", "Graphite"
};

static int dark_mode;
static int accent_idx;

const theme_t *theme(void)                 { return dark_mode ? &dark : &light; }
const theme_t *theme_variant(int d)        { return d ? &dark : &light; }
int  theme_is_dark(void)                   { return dark_mode; }
void theme_set_dark(int d)                 { dark_mode = d ? 1 : 0; }

u32  theme_accent(void)                    { return accent_colors[accent_idx]; }
int  theme_accent_index(void)              { return accent_idx; }
void theme_set_accent(int i)               { if (i >= 0 && i < ACCENT_COUNT) accent_idx = i; }
u32  theme_accent_color(int i)             { return accent_colors[i]; }
const char *theme_accent_name(int i)       { return accent_names[i]; }
