/* ---------- Spotlight и уведомления (включается из wm.c) ---------- */

#define SP_W 620
#define SP_IN_H 58
#define SP_ROW 40
#define SP_MAXR 8
enum { SR_APP, SR_FILE, SR_DIR, SR_CALC };
typedef struct { u8 kind; i16 app; int node; char text[48]; } sres_t;

static int  sp_on;
static i32  sp_v;
static char sp_q[40];
static int  sp_qn, sp_n, sp_sel;
static sres_t sp_r[SP_MAXR];

static int sp_has(const char *h, const char *n)                   /* n входит в h без учёта регистра */
{
    if (!*n) return 0;
    for (; *h; h++) {
        int k = 0;
        while (n[k] && h[k] && lc(h[k]) == lc(n[k])) k++;
        if (!n[k]) return 1;
    }
    return 0;
}

static void sp_add(int kind, int app, int node, const char *text)
{
    if (sp_n >= SP_MAXR) return;
    sres_t *r = &sp_r[sp_n++];
    r->kind = (u8)kind; r->app = (i16)app; r->node = node;
    int i = 0;
    while (text[i] && i < 46) { r->text[i] = text[i]; i++; }
    r->text[i] = 0;
}

static void sp_walk(int dir, int depth)
{
    int list[64];
    int n = fs_list(dir, list, 64);
    for (int i = 0; i < n && sp_n < SP_MAXR; i++) {
        if (fs_name(list[i])[0] != '.' && sp_has(fs_name(list[i]), sp_q)) sp_add(fs_is_dir(list[i]) ? SR_DIR : SR_FILE, -1, list[i], fs_name(list[i]));
        if (fs_is_dir(list[i]) && depth < 5) sp_walk(list[i], depth + 1);
    }
}

static void sp_search(void)
{
    sp_n = 0;
    sp_sel = 0;
    if (!sp_qn) return;
    int op = 0;
    for (int i = 0; i < sp_qn; i++)
        if (sp_q[i] == '+' || sp_q[i] == '*' || sp_q[i] == '/' || sp_q[i] == '%' || (i > 0 && sp_q[i] == '-')) op = 1;
    if (op && ((sp_q[0] >= '0' && sp_q[0] <= '9') || sp_q[0] == '(' || sp_q[0] == '-')) {
        cp_ = sp_q; c_err = 0;
        i32 v = c_expr();
        c_ws();
        if (!*cp_ && !c_err) {
            char b[48], *q = b;
            for (int i = 0; i < sp_qn && i < 28; i++) *q++ = sp_q[i];
            *q++ = ' '; *q++ = '='; *q++ = ' ';
            u32 u = (u32)v;
            if (v < 0) { *q++ = '-'; u = (u32)(-v); }
            q = u_put(q, u);
            *q = 0;
            sp_add(SR_CALC, -1, -1, b);
        }
    }
    for (int a = 0; a < APP_COUNT; a++)
        if (sp_has(app_name[a], sp_q)) sp_add(SR_APP, a, -1, app_name[a]);
    if (sp_has("Launchpad", sp_q)) sp_add(SR_APP, -2, -1, "Launchpad");
    if (sp_has("Install EndixOS", sp_q) || sp_has("Installer", sp_q)) sp_add(SR_APP, -4, -1, "Install EndixOS");
    if (sp_has("Mission Control", sp_q)) sp_add(SR_APP, -3, -1, "Mission Control");
    sp_walk(fs_home(), 0);
}

static void sp_close(void) { sp_on = 0; }

static void sp_toggle(void)
{
    if (sp_on) { sp_close(); return; }
    if (mc_on) mc_close_now(-1);
    if (lp_open) lp_close();
    cc_close();
    menu_close();
    desktop_ctx_close();
    sp_on = 1;
    sp_qn = 0; sp_q[0] = 0; sp_n = 0; sp_sel = 0;
}

static void sp_run(int i)
{
    if (i < 0 || i >= sp_n) return;
    sres_t *r = &sp_r[i];
    if (r->kind == SR_CALC) return;
    sp_close();
    if (r->kind == SR_APP) {
        if (r->app >= 0) open_window(r->app);
        else if (r->app == -2) lp_show();
        else if (r->app == -4) { installer_open(); scene_dirty = 1; }
        else mc_toggle();
    } else {
        open_window(APP_FILES);
        files_goto(r->node);
    }
}

static void sp_key(i32 c)
{
    if (c == 27) { sp_close(); return; }
    if (c == '\n') { sp_run(sp_sel); return; }
    if (c == KEY_UP) { if (sp_n) sp_sel = (sp_sel + sp_n - 1) % sp_n; return; }
    if (c == KEY_DOWN) { if (sp_n) sp_sel = (sp_sel + 1) % sp_n; return; }
    if (c == '\b') { if (sp_qn) { sp_q[--sp_qn] = 0; sp_search(); } return; }
    if (c >= 32 && c < 127 && sp_qn < 38) { sp_q[sp_qn++] = (char)c; sp_q[sp_qn] = 0; sp_search(); }
}

static i32 sp_x(void) { return ((i32)fb_width() - SP_W) / 2; }
static i32 sp_y(void) { return (i32)fb_height() / 5; }
static i32 sp_h(void) { return SP_IN_H + (sp_n ? 12 + sp_n * SP_ROW : 0); }

static int sp_row_at(i32 x, i32 y)
{
    i32 px = sp_x(), py = sp_y();
    if (x < px || x >= px + SP_W) return -1;
    for (int i = 0; i < sp_n; i++) {
        i32 ry = py + SP_IN_H + 6 + i * SP_ROW;
        if (y >= ry && y < ry + SP_ROW) return i;
    }
    return -1;
}

static int sp_inside(i32 x, i32 y) { return x >= sp_x() && x < sp_x() + SP_W && y >= sp_y() && y < sp_y() + sp_h(); }

static void sp_draw(void)
{
    if (sp_v <= 0) return;
    int anim = sp_v < 1024;
    if (anim) anim_screen_save();
    const theme_t *t = theme();
    i32 px = sp_x(), py = sp_y() - (1024 - sp_v) * 14 / 1024, h = sp_h();
    fb_glass(px, py, SP_W, h, 24, GLASS_ALL, 12, theme_is_dark() ? RGB(40, 40, 46) : RGB(246, 246, 249),
             theme_is_dark() ? 150 : 165, 26);
    cc_draw_search_icon(px + 22, py + (SP_IN_H - 16) / 2, t->sub);
    if (sp_qn) {
        font_draw(px + 56, py + (SP_IN_H - font_height[FONT_TITLE]) / 2, sp_q, t->text, FONT_TITLE);
        if ((now_ticks / 50) % 2 == 0) {
            i32 cx = px + 56 + font_width(sp_q, FONT_TITLE) + 2;
            fb_fill_rect(cx, py + 15, 2, SP_IN_H - 30, theme_accent());
        }
    } else {
        font_draw(px + 56, py + (SP_IN_H - font_height[FONT_TITLE]) / 2, "Spotlight Search", t->sub, FONT_TITLE);
    }
    if (sp_n) {
        fb_blend_rect(px + 14, py + SP_IN_H, SP_W - 28, 1, RGB(0, 0, 0), t->line_alpha);
        for (int i = 0; i < sp_n; i++) {
            sres_t *r = &sp_r[i];
            i32 ry = py + SP_IN_H + 6 + i * SP_ROW;
            int s = i == sp_sel;
            if (s) fb_fill_round_rect(px + 8, ry, SP_W - 16, SP_ROW - 2, 10, theme_accent(), 255);
            u32 col = s ? RGB(255, 255, 255) : t->text, sub = s ? RGB(230, 230, 240) : t->sub;
            i32 ix = px + 20, iy = ry + 4;
            const char *kind = "";
            if (r->kind == SR_APP) {
                icon_draw(r->app >= 0 ? r->app : r->app == -2 ? ICON_LAUNCHPAD : ICON_TILE_APPEAR, ix, iy, 30);
                kind = "Application";
            } else if (r->kind == SR_CALC) {
                fb_fill_round_rect(ix, iy, 30, 30, 8, RGB(52, 199, 89), 255);
                font_draw(ix + 10, iy + 5, "=", RGB(255, 255, 255), FONT_BOLD);
                kind = "Calculator";
            } else {
                icon_draw(r->kind == SR_DIR ? ICON_DESK_FOLDER : ICON_DESK_FILE, ix, iy, 30);
                kind = r->kind == SR_DIR ? "Folder" : "Document";
            }
            font_draw(px + 62, ry + 8, r->text, col, FONT_REGULAR);
            font_draw(px + SP_W - 24 - font_width(kind, FONT_REGULAR), ry + 8, kind, sub, FONT_REGULAR);
        }
    }
    if (anim) anim_screen_blend(sp_v);
}

/* ---------- уведомления ---------- */
/* Стопка баннеров справа сверху (до трёх, новый сверху). Наведение останавливает таймер и показывает крестик, клик закрывает. */

#define NT_W 340
#define NT_H 74
#define NT_GAP 10
#define NT_MAX 3
#define NT_LIFE 380                                /* тиков: 3.8 с */
typedef struct { char title[32], text[80]; u32 t0; int on, icon; i32 v, yq; } nt_t;
static nt_t nts[NT_MAX];

static void notify_icon(const char *title, const char *text, int icon)
{
    for (int k = NT_MAX - 1; k > 0; k--) nts[k] = nts[k - 1];     /* остальные сдвигаются вниз */
    nt_t *n = &nts[0];
    int i = 0;
    while (title[i] && i < 30) { n->title[i] = title[i]; i++; }
    n->title[i] = 0;
    i = 0;
    while (text[i] && i < 78) { n->text[i] = text[i]; i++; }
    n->text[i] = 0;
    n->t0 = now_ticks; n->on = 1; n->icon = icon; n->v = 0; n->yq = 0;
    scene_dirty = 1;
}

static void notify(const char *title, const char *text) { notify_icon(title, text, -1); }
void wm_notify(const char *title, const char *text) { notify(title, text); }

static i32 nt_y(int k) { return MENUBAR_H + 10 + nts[k].yq * (NT_H + NT_GAP) / 1024; }

static int nt_at(i32 x, i32 y)
{
    i32 W = (i32)fb_width();
    for (int k = 0; k < NT_MAX; k++) {
        if (!nts[k].on) continue;
        i32 ty = nt_y(k);
        if (x >= W - NT_W - 14 && x < W - 14 && y >= ty && y < ty + NT_H) return k;
    }
    return -1;
}

static int nt_tick(u32 dt)
{
    int moving = 0, slot = 0;
    int hot = nt_at(cur_x, cur_y);
    for (int k = 0; k < NT_MAX; k++) {
        nt_t *n = &nts[k];
        if (n->on && k == hot) n->t0 = now_ticks;                         /* пока курсор на баннере, он не исчезает */
        if (n->on && (i32)(now_ticks - n->t0) > NT_LIFE) n->on = 0;
        i32 v = anim_approach(n->v, n->on ? 1024 : 0, dt);
        i32 q = anim_approach(n->yq, slot * 1024, dt);
        if (v != n->v || q != n->yq || n->on) moving = 1;
        n->v = v; n->yq = q;
        if (n->on || n->v > 0) slot++;
    }
    return moving;
}

static int nt_hit(i32 x, i32 y) { return nt_at(x, y) >= 0; }
static void nt_dismiss_at(i32 x, i32 y) { int k = nt_at(x, y); if (k >= 0) nts[k].on = 0; }

static void nt_draw(void)
{
    const theme_t *t = theme();
    i32 W = (i32)fb_width();
    int hot = nt_at(cur_x, cur_y);
    for (int k = NT_MAX - 1; k >= 0; k--) {
        nt_t *n = &nts[k];
        if (n->v <= 0) continue;
        i32 off = (1024 - n->v) * (NT_W + 30) / 1024;
        i32 x = W - NT_W - 14 + off, y = nt_y(k);
        fb_glass(x, y, NT_W, NT_H, 22, GLASS_ALL, 14, theme_is_dark() ? RGB(44, 44, 50) : RGB(246, 246, 249),
                 theme_is_dark() ? 120 : 130, 30);
        if (n->icon >= 0) icon_draw(n->icon, x + 14, y + 17, 40);
        else {
            fb_fill_round_rect(x + 14, y + 17, 40, 40, 11, theme_accent(), 255);
            logo_draw(x + 14 + 10, y + 17 + 10, 20, RGB(255, 255, 255));
        }
        font_draw(x + 66, y + 14, n->title, t->text, FONT_BOLD);
        char b[80];
        int c = 0;
        while (n->text[c] && c < 78) { b[c] = n->text[c]; c++; }
        b[c] = 0;
        while (c > 1 && font_width(b, FONT_REGULAR) > NT_W - 80) {
            c--;
            while (c > 1 && ((u8)b[c] & 0xC0) == 0x80) c--;
            b[c] = 0;
        }
        font_draw(x + 66, y + 38, b, t->sub, FONT_REGULAR);
        if (k == hot) {                                                    /* крестик закрытия */
            fb_fill_circle(x + 8, y + 8, 9, theme_is_dark() ? RGB(70, 70, 78) : RGB(222, 222, 228));
            for (i32 d = -3; d <= 3; d++) {
                fb_fill_rect(x + 8 + d, y + 8 + d, 1, 1, t->text);
                fb_fill_rect(x + 8 + d, y + 8 - d, 1, 1, t->text);
            }
        } else {
            font_draw(x + NT_W - 14 - font_width("now", FONT_REGULAR), y + 14, "now", t->sub, FONT_REGULAR);
        }
    }
}
