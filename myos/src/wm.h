#ifndef WM_H
#define WM_H
#include <stdint.h>

#define WM_MAX_WIN 8
#define WM_TITLE_MAX 32

typedef void (*wm_draw_fn)(int x, int y, int w, int h, void* user);

typedef struct wm_window {
    int          used;
    int          x, y, w, h;
    char         title[WM_TITLE_MAX];
    uint32_t     border_color;
    uint32_t     title_color;
    uint32_t     bg_color;
    wm_draw_fn   draw;
    void*        user;
} wm_window;

void wm_init(void);
int  wm_create(const char* title, int x, int y, int w, int h, wm_draw_fn fn, void* user);
void wm_close(int id);
void wm_focus_next(void);
void wm_focus_prev(void);
int  wm_focused(void);
void wm_render(void);
void wm_move_focused(int dx, int dy);
void wm_clear_all(void);
int  wm_hit_test(int x, int y);

/* Доступ к окну по индексу — для таскбара */
int  wm_is_used(int id);
int  wm_is_focused(int id);
const char* wm_title(int id);

/* Новое */
int  wm_hit_test(int x, int y);           /* -1 если не попал, иначе id окна */
int  wm_close_button_hit(int x, int y);   /* -1 или id окна, где кликнули в [X] */
void wm_raise(int id);

/* macOS traffic lights: x — координаты клика, возвращает id или -1 */
int  wm_close_button_hit(int x, int y);
int  wm_minimize_button_hit(int x, int y);
int  wm_maximize_button_hit(int x, int y);

void wm_minimize(int id);
void wm_maximize(int id);
int  wm_is_minimized(int id);                     /* на верх */

#endif
