#include "files.h"
#include "wm.h"
#include "keyboard.h"
#include "framebuffer.h"
#include "fat16.h"
#include "tty.h"
#include <stdint.h>

#define MAX_FILES 32
#define NAME_MAX  13

struct file_entry {
    char     name[NAME_MAX];
    uint32_t size;
    int      is_dir;
};

static struct file_entry files[MAX_FILES];
static int n_files = 0;
static int sel     = 0;
static int scroll  = 0;

static int ls_cb(const struct fat16_dirent* de, void* user) {
    (void)user;
    if (n_files >= MAX_FILES) return 1;
    fat16_name(de, files[n_files].name);
    files[n_files].size   = de->size;
    files[n_files].is_dir = (de->attr & FAT16_ATTR_DIR) ? 1 : 0;
    n_files++;
    return 0;
}

void files_init(void) { n_files = 0; sel = 0; scroll = 0; }

static void files_refresh(void) {
    n_files = 0;
    fat16_ls(ls_cb, 0);
}

/* Нарисовать содержимое окна — вызывается из WM как callback */
static void files_draw(int x, int y, int w, int h, void* user) {
    (void)user;
    files_refresh();

    #define F_SURF   0x1C1B1F
    #define F_SURF2  0x2B2930
    #define F_PRIM   0xD0BCFF
    #define F_TER    0xEFB8C8
    #define F_ON     0xE6E1E5
    #define F_VAR    0xCAC4D0
    #define F_OUT    0x49454F

    int line_h = 40;
    int pad    = 8;
    int visible = (h - 40) / line_h;

    gfx_puts(x + 16, y + 8, "NAME", F_VAR, 1);
    gfx_puts(x + 200, y + 8, "SIZE", F_VAR, 1);

    if (sel < scroll) scroll = sel;
    if (sel >= scroll + visible) scroll = sel - visible + 1;

    for (int i = 0; i < visible; i++) {
        int idx = scroll + i;
        if (idx >= n_files) break;

        int yy = y + 30 + i * line_h;
        int cx_rect = x + pad;
        int cy_rect = yy;

        if (idx == sel) {
            /* Подсветка — закруглённый прямоугольник */
            gfx_rounded_fill(cx_rect, cy_rect, w - 2*pad, line_h - 4, 12, F_SURF2);
        }

        /* Иконка файла/папки */
        uint32_t ic = files[idx].is_dir ? F_PRIM : F_TER;
        int ix = cx_rect + 14, iy = cy_rect + 12;
        if (files[idx].is_dir) {
            gfx_rounded_fill(ix, iy, 14, 14, 4, ic);
        } else {
            gfx_rounded_fill(ix + 2, iy + 1, 10, 12, 2, ic);
        }

        gfx_puts(cx_rect + 40, cy_rect + 14, files[idx].name,
                 (idx == sel) ? F_ON : F_VAR, 1);

        /* Размер */
        char sz[12];
        sz[0]='0'; sz[1]='x';
        uint32_t s = files[idx].size;
        for (int k = 0; k < 8; k++) {
            uint32_t nib = (s >> ((7-k)*4)) & 0xF;
            sz[2+k] = nib < 10 ? ('0'+nib) : ('a'+nib-10);
        }
        sz[10] = 0;
        gfx_puts(x + 200, cy_rect + 14, sz,
                 (idx == sel) ? F_ON : F_VAR, 1);
    }
}







static int file_win_id = -1;

void files_open(void) {
    if (file_win_id >= 0) {
        int f = wm_focused();
        (void)f;
        wm_raise(file_win_id);
        return;
    }
    file_win_id = wm_create("Files",
                            200, 150, 400, 400,
                            files_draw, 0);
    wm_render();
}

void files_handle_key(int c) {
    if (file_win_id < 0) return;
    if (wm_focused() != file_win_id) return;

    if (c == KEY_UP)   { if (sel > 0) sel--; wm_render(); }
    else if (c == KEY_DOWN) {
        if (sel + 1 < n_files) sel++;
        wm_render();
    }
    else if (c == '\n') {
        if (sel >= 0 && sel < n_files) {
            /* TODO: открыть файл — покажем содержимое в новом окне */
            tty_puts("[files] open "); tty_puts(files[sel].name); tty_putc('\n');
        }
    }
}
