/* Команды терминала: текст, система, оформление, развлечения.
 * Файл подключается в wm.c и использует его term_puts, term_err, fs_* и прочее. */

static void term_cputs(const char *s, u8 a)               /* строка заданным цветом */
{
    u8 o = tattr;
    tattr = a;
    term_puts(s);
    tattr = o;
}

static char lower_c(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static int s_eq_i(const char *a, const char *b)           /* без учёта регистра */
{
    while (*a && lower_c(*a) == lower_c(*b)) { a++; b++; }
    return *a == *b;
}

static int s_starts(const char *s, const char *pre)
{
    while (*pre && *s == *pre) { s++; pre++; }
    return *pre == 0;
}

static int s_atoi(const char *s, int *out)                /* -1, если это не число */
{
    int neg = 0, n = 0, any = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; any = 1; if (n > 100000000) break; }
    if (!any || *s) return -1;
    *out = neg ? -n : n;
    return 0;
}

/* ---------- источник данных: файл или вход после | ---------- */

static int term_src(const char *cmd, const char *path, const u8 **d, u32 *n)
{
    if (path) {
        int node = fs_resolve(cwd, path);
        if (node < 0) { term_err(cmd, path, FS_ENOENT); return -1; }
        if (fs_is_dir(node)) {
            term_puts(cmd); term_puts(": "); term_puts(path); term_puts(": is a directory\n");
            return -1;
        }
        *d = fs_data(node);
        *n = fs_size(node);
        return 0;
    }
    if (tin) { *d = (const u8 *)tin; *n = (u32)tinn; return 0; }
    term_puts(cmd); term_puts(": missing file operand\n");
    return -1;
}

static int next_line(const u8 *d, u32 n, u32 *pos, const u8 **ln, u32 *len)
{
    if (*pos >= n) return 0;
    u32 s = *pos, e = s;
    while (e < n && d[e] != '\n') e++;
    *ln = d + s;
    *len = e - s;
    *pos = e < n ? e + 1 : e;
    return 1;
}

static void put_line(const u8 *l, u32 len)
{
    for (u32 i = 0; i < len; i++) {
        u8 c = l[i];
        term_putc(c == '\t' ? ' ' : (c >= 32 && c < 127 ? (char)c : '.'));
    }
    term_putc('\n');
}

static int line_has(const u8 *l, u32 len, const char *pat, int icase)
{
    int pl = s_len(pat);
    if (pl == 0) return 1;
    for (u32 i = 0; i + (u32)pl <= len; i++) {
        int ok = 1;
        for (int k = 0; k < pl && ok; k++) {
            char a = (char)l[i + (u32)k], b = pat[k];
            if (icase) { a = lower_c(a); b = lower_c(b); }
            if (a != b) ok = 0;
        }
        if (ok) return 1;
    }
    return 0;
}

static void cmd_grep(char *args)
{
    char *p = args, *a;
    int icase = 0, num = 0;
    const char *pat = 0;
    const char *files[4];
    int nf = 0;
    while ((a = next_tok(&p)) != 0) {
        if (a[0] == '-' && a[1] && !pat) {
            for (int i = 1; a[i]; i++) { if (a[i] == 'i') icase = 1; else if (a[i] == 'n') num = 1; }
        } else if (!pat) pat = a;
        else if (nf < 4) files[nf++] = a;
    }
    if (!pat) { term_puts("usage: grep [-i] [-n] text [file...]\n"); return; }
    for (int f = 0; f < (nf ? nf : 1); f++) {
        const u8 *d; u32 n;
        if (term_src("grep", nf ? files[f] : 0, &d, &n) < 0) continue;
        u32 pos = 0, len, ln = 0;
        const u8 *l;
        while (next_line(d, n, &pos, &l, &len)) {
            ln++;
            if (!line_has(l, len, pat, icase)) continue;
            char b[12];
            if (nf > 1) { term_cputs(files[f], TC_CYAN); term_putc(':'); }
            if (num) { u_put(b, ln); term_cputs(b, TC_GREEN); term_putc(':'); }
            put_line(l, len);
        }
    }
}

static int opt_count(char *args, char **file, int def)    /* -n N или -N; файл - первый остальной аргумент */
{
    char *p = args, *a;
    int n = def;
    *file = 0;
    while ((a = next_tok(&p)) != 0) {
        if (s_eq(a, "-n")) {
            char *v = next_tok(&p);
            int x;
            if (v && s_atoi(v, &x) == 0 && x >= 0) n = x;
        } else if (a[0] == '-' && a[1] >= '0' && a[1] <= '9') {
            int x;
            if (s_atoi(a + 1, &x) == 0) n = x;
        } else if (!*file) *file = a;
    }
    return n;
}

static void cmd_head_tail(int tail, char *args)
{
    char *f;
    int cnt = opt_count(args, &f, 10);
    const u8 *d; u32 n;
    if (term_src(tail ? "tail" : "head", f, &d, &n) < 0) return;
    u32 pos = 0, len, total = 0;
    const u8 *l;
    while (next_line(d, n, &pos, &l, &len)) total++;
    pos = 0;
    u32 i = 0, from = tail ? (total > (u32)cnt ? total - (u32)cnt : 0) : 0;
    u32 to = tail ? total : (u32)cnt;
    while (next_line(d, n, &pos, &l, &len)) {
        if (i >= from && i < to) put_line(l, len);
        i++;
    }
}

static void cmd_wc(char *args)
{
    char *p = args, *a, *f = 0;
    int ol = 0, ow = 0, oc = 0;
    while ((a = next_tok(&p)) != 0) {
        if (a[0] == '-' && a[1]) {
            for (int i = 1; a[i]; i++) { if (a[i] == 'l') ol = 1; else if (a[i] == 'w') ow = 1; else if (a[i] == 'c') oc = 1; }
        } else f = a;
    }
    if (!ol && !ow && !oc) ol = ow = oc = 1;
    const u8 *d; u32 n;
    if (term_src("wc", f, &d, &n) < 0) return;
    u32 lines = 0, words = 0, inw = 0;
    for (u32 i = 0; i < n; i++) {
        if (d[i] == '\n') lines++;
        int sp = d[i] == ' ' || d[i] == '\n' || d[i] == '\t';
        if (!sp && !inw) words++;
        inw = !sp;
    }
    char b[16];
    if (ol) { u_put(b, lines); term_puts(b); term_putc(' '); }
    if (ow) { u_put(b, words); term_puts(b); term_putc(' '); }
    if (oc) { u_put(b, n); term_puts(b); term_putc(' '); }
    if (f) term_puts(f);
    term_putc('\n');
}

static void cmd_sort(char *args)
{
    char *p = args, *a, *f = 0;
    int rev = 0;
    while ((a = next_tok(&p)) != 0) {
        if (s_eq(a, "-r")) rev = 1; else f = a;
    }
    const u8 *d; u32 n;
    if (term_src("sort", f, &d, &n) < 0) return;
    const u8 *ls[128]; u32 ll[128];
    int cnt = 0;
    u32 pos = 0, len;
    const u8 *l;
    while (cnt < 128 && next_line(d, n, &pos, &l, &len)) { ls[cnt] = l; ll[cnt] = len; cnt++; }
    for (int i = 1; i < cnt; i++) {
        const u8 *kl = ls[i]; u32 kn = ll[i];
        int j = i - 1;
        for (; j >= 0; j--) {
            u32 m = ll[j] < kn ? ll[j] : kn, k = 0;
            while (k < m && ls[j][k] == kl[k]) k++;
            int cmp = k < m ? (int)ls[j][k] - (int)kl[k] : (int)ll[j] - (int)kn;
            if (rev ? cmp >= 0 : cmp <= 0) break;
            ls[j + 1] = ls[j]; ll[j + 1] = ll[j];
        }
        ls[j + 1] = kl; ll[j + 1] = kn;
    }
    for (int i = 0; i < cnt; i++) put_line(ls[i], ll[i]);
}

/* ---------- дерево и поиск ---------- */

static void tree_walk(int dir, int depth, char *prefix)
{
    if (depth > 6) return;
    int list[FS_MAX_NODES];
    int n = fs_list(dir, list, FS_MAX_NODES);
    for (int i = 0; i < n; i++) {
        int last = i == n - 1, isd = fs_is_dir(list[i]);
        term_cputs(prefix, TC_GRAY);
        term_cputs(last ? "`-- " : "|-- ", TC_GRAY);
        term_cputs(fs_name(list[i]), isd ? TC_BLUE : TC_DEF);
        if (isd) term_putc('/');
        term_putc('\n');
        if (isd) {
            int pl = s_len(prefix);
            if (pl < 40) {
                s_put(prefix + pl, last ? "    " : "|   ");
                tree_walk(list[i], depth + 1, prefix);
                prefix[pl] = 0;
            }
        }
    }
}

static void find_walk(int dir, const char *pat, int *found)
{
    int list[FS_MAX_NODES];
    int n = fs_list(dir, list, FS_MAX_NODES);
    for (int i = 0; i < n; i++) {
        const char *nm = fs_name(list[i]);
        if (line_has((const u8 *)nm, (u32)s_len(nm), pat, 1)) {
            char path[96];
            fs_path(list[i], path, sizeof(path));
            term_cputs(path, fs_is_dir(list[i]) ? TC_BLUE : TC_DEF);
            term_putc('\n');
            (*found)++;
        }
        if (fs_is_dir(list[i]))
            find_walk(list[i], pat, found);
    }
}

/* ---------- календарь и калькулятор ---------- */

static int days_in_month(u32 y, u32 m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

static void cmd_cal(char *args)
{
    char *p = args;
    char *a1 = next_tok(&p), *a2 = next_tok(&p);
    u32 m = t_mon < 1 || t_mon > 12 ? 1 : t_mon, y = t_year ? t_year : 2026;
    int x;
    if (a1 && a2 && s_atoi(a1, &x) == 0 && x >= 1 && x <= 12) {
        m = (u32)x;
        if (s_atoi(a2, &x) == 0 && x >= 1900 && x <= 9999) y = (u32)x;
    } else if (a1 && s_atoi(a1, &x) == 0 && x >= 1900 && x <= 9999) y = (u32)x;
    static const char *const full[12] = { "January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December" };
    char hdr[40], *h = s_put(hdr, full[m - 1]);
    *h++ = ' ';
    h = u_put(h, y);
    for (int i = (20 - s_len(hdr)) / 2; i > 0; i--) term_putc(' ');
    term_cputs(hdr, TC_ACCENT);
    term_putc('\n');
    term_cputs("Su Mo Tu We Th Fr Sa\n", TC_GRAY);
    int dow = day_of_week(y, m, 1);
    for (int i = 0; i < dow; i++) term_puts("   ");
    int dim = days_in_month(y, m);
    for (int d = 1; d <= dim; d++) {
        char b[4];
        u2_put(b, (u32)d);
        if (b[0] == '0') b[0] = ' ';
        int today = have_time && (u32)d == t_day && m == t_mon && y == t_year;
        term_cputs(b, today ? TC_ACCENT : TC_DEF);
        term_putc(' ');
        if ((dow + d) % 7 == 0 || d == dim) term_putc('\n');
    }
}

static const char *cp_;
static int c_err;
static i32 c_expr(void);
static void c_ws(void) { while (*cp_ == ' ') cp_++; }

static i32 c_factor(void)
{
    c_ws();
    if (*cp_ == '-') { cp_++; return -c_factor(); }
    if (*cp_ == '+') { cp_++; return c_factor(); }
    if (*cp_ == '(') {
        cp_++;
        i32 v = c_expr();
        c_ws();
        if (*cp_ == ')') cp_++; else c_err = 1;
        return v;
    }
    if (*cp_ >= '0' && *cp_ <= '9') {
        i32 v = 0;
        while (*cp_ >= '0' && *cp_ <= '9') { v = v * 10 + (*cp_ - '0'); cp_++; }
        return v;
    }
    c_err = 1;
    return 0;
}

static i32 c_term(void)
{
    i32 v = c_factor();
    for (;;) {
        c_ws();
        char o = *cp_;
        if (o == '*') { cp_++; v *= c_factor(); }
        else if (o == '/' || o == '%') {
            cp_++;
            i32 r = c_factor();
            if (r == 0) { c_err = 2; return 0; }
            v = o == '/' ? v / r : v % r;
        } else return v;
    }
}

static i32 c_expr(void)
{
    i32 v = c_term();
    for (;;) {
        c_ws();
        if (*cp_ == '+') { cp_++; v += c_term(); }
        else if (*cp_ == '-') { cp_++; v -= c_term(); }
        else return v;
    }
}

static void cmd_calc(char *args)
{
    if (!*args) { term_puts("usage: calc 2+2*(3-1)\n"); return; }
    cp_ = args;
    c_err = 0;
    i32 v = c_expr();
    c_ws();
    if (*cp_ && !c_err) c_err = 1;
    if (c_err == 2) { term_cputs("calc: division by zero\n", TC_RED); return; }
    if (c_err) { term_cputs("calc: syntax error\n", TC_RED); return; }
    char b[16], *q = b;
    if (v < 0) { *q++ = '-'; v = -v; }
    u_put(q, (u32)v);
    term_cputs(b, TC_YELLOW);
    term_putc('\n');
}

/* ---------- система ---------- */

static const char *const proc_names[] = { "kernel", "windowserver", "dock", "menubar" };

static int app_by_name(const char *n)
{
    for (int i = 0; i < APP_COUNT; i++)
        if (s_eq_i(n, app_name[i])) return i;
    return -1;
}

static void cmd_ps(void)
{
    term_cputs("  PID  NAME          STATE\n", TC_GRAY);
    char b[16];
    for (int i = 0; i < 4; i++) {
        char *q = u_put(b, (u32)i + 1);
        (void)q;
        for (int k = s_len(b); k < 5; k++) term_putc(' ');
        term_puts(b);
        term_puts("  ");
        term_puts(proc_names[i]);
        for (int k = s_len(proc_names[i]); k < 14; k++) term_putc(' ');
        term_cputs("running\n", TC_GREEN);
    }
    for (int i = 0; i < APP_COUNT; i++) {
        if (!wins[i].open) continue;
        u_put(b, 100 + (u32)i);
        for (int k = s_len(b); k < 5; k++) term_putc(' ');
        term_puts(b);
        term_puts("  ");
        char nm[16];
        int k = 0;
        for (; app_name[i][k] && k < 15; k++) nm[k] = lower_c(app_name[i][k]);
        nm[k] = 0;
        term_puts(nm);
        for (int q = k; q < 14; q++) term_putc(' ');
        if (wins[i].minimized) term_cputs("minimized\n", TC_YELLOW);
        else term_cputs("running\n", TC_GREEN);
    }
}

static void cmd_kill(char *args)
{
    char *p = args, *a = next_tok(&p);
    if (!a) { term_puts("usage: kill name|pid\n"); return; }
    int pid, app = -1;
    if (s_atoi(a, &pid) == 0) { if (pid >= 100 && pid < 100 + APP_COUNT) app = pid - 100; }
    else app = app_by_name(a);
    if (app < 0 || !wins[app].open) {
        term_puts("kill: no such process: ");
        term_puts(a);
        term_putc('\n');
        return;
    }
    close_window(app);
}

static void cmd_open(char *args)
{
    char *p = args, *a = next_tok(&p);
    if (!a) { term_puts("usage: open files|terminal|settings|snake|about|launchpad|mission\n"); return; }
    if (s_eq_i(a, "launchpad")) { lp_show(); return; }
    if (s_eq_i(a, "mission") || s_eq_i(a, "missioncontrol")) { mc_toggle(); return; }
    int app = app_by_name(a);
    if (app < 0) {
        term_puts("open: cannot open: ");
        term_puts(a);
        term_putc('\n');
        return;
    }
    open_window(app);
}

static void cmd_df(void)
{
    int nodes = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) if (fs_valid(i)) nodes++;
    char b[16];
    term_cputs("Filesystem   Used     Size     Nodes\n", TC_GRAY);
    term_puts("memfs        ");
    u_put(b, fs_used_bytes() / 1024); term_puts(b); term_puts(" KB");
    for (int k = s_len(b); k < 5; k++) term_putc(' ');
    u_put(b, FS_POOL_SIZE / 1024); term_puts(b); term_puts(" KB");
    for (int k = s_len(b); k < 5; k++) term_putc(' ');
    u_put(b, (u32)nodes); term_puts(b); term_putc('/');
    u_put(b, FS_MAX_NODES); term_puts(b); term_putc('\n');
}

/* ---------- neofetch ---------- */

static void cmd_neofetch(void)
{
    char logo[14][34];
    for (int y = 0; y < 14; y++) {
        for (int x = 0; x < 30; x++) {
            i32 X = 2 * x - 29, Y = 4 * y - 26;
            i32 ax = X < 0 ? -X : X, ay = Y < 0 ? -Y : Y;
            i32 dx = ax - 10, dy = ay - 10;                 /* четыре круга по диагоналям: клевер */
            logo[y][x] = (dx * dx + dy * dy <= 12 * 12) ? '#' : ' ';
        }
        logo[y][30] = 0;
    }
    char info[14][64];
    int n = 0;
    char *q;
    q = s_put(info[n], "endix@v8"); n++;
    q = s_put(info[n], "--------"); n++;
    q = s_put(info[n], "OS: EndixOS "); s_put(q, ENDIX_VERSION); n++;
    s_put(info[n], "Host: EndixOS PC"); n++;
    s_put(info[n], "Kernel: own, i686, from scratch"); n++;
    q = s_put(info[n], "Uptime: "); q = u_put(q, uptime_s / 60); q = s_put(q, " min "); q = u_put(q, uptime_s % 60); s_put(q, " s"); n++;
    q = s_put(info[n], "Resolution: "); q = u_put(q, sys.width); q = s_put(q, "x"); u_put(q, sys.height); n++;
    q = s_put(info[n], "CPU: "); s_put(q, sys.cpu); n++;
    q = s_put(info[n], "Memory: "); q = u_put(q, sys.ram_mb); s_put(q, " MB"); n++;
    q = s_put(info[n], "Theme: "); q = s_put(q, theme_is_dark() ? "Dark, " : "Light, "); s_put(q, theme_accent_name(theme_accent_index())); n++;
    q = s_put(info[n], "Wallpaper: "); s_put(q, wallpaper_name(settings_wallpaper())); n++;
    int open = 0;
    for (int i = 0; i < APP_COUNT; i++) if (wins[i].open) open++;
    q = s_put(info[n], "Windows: "); u_put(q, (u32)open); n++;
    for (int i = 0; i < 14; i++) {
        term_cputs(logo[i], TC_ACCENT);
        term_puts("  ");
        if (i < n) {
            char *colon = info[i];
            while (*colon && *colon != ':') colon++;
            if (i == 0) term_cputs(info[i], TC_ACCENT);
            else if (*colon == ':') {
                char key[24];
                int kl = (int)(colon - info[i]) + 1;
                for (int k = 0; k < kl && k < 23; k++) key[k] = info[i][k];
                key[kl < 23 ? kl : 23] = 0;
                term_cputs(key, TC_ACCENT);
                term_puts(colon + 1);
            } else term_cputs(info[i], TC_GRAY);
        } else if (i == n + 1) {
            for (int c = 1; c <= 8; c++) { term_cputs("###", (u8)c); }
        }
        term_putc('\n');
    }
}

/* ---------- развлечения ---------- */

static void cmd_cowsay(char *args)
{
    char text[80];
    int tl = 0;
    if (*args) { while (args[tl] && tl < 60) { text[tl] = args[tl]; tl++; } }
    else if (tin) { while (tl < tinn && tl < 60 && tin[tl] != '\n') { text[tl] = tin[tl]; tl++; } }
    else { s_put(text, "Moo!"); tl = 4; }
    text[tl] = 0;
    term_putc(' ');
    for (int i = 0; i < tl + 2; i++) term_putc('_');
    term_puts("\n< ");
    term_puts(text);
    term_puts(" >\n ");
    for (int i = 0; i < tl + 2; i++) term_putc('-');
    term_puts("\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
}

static void cmd_fortune(void)
{
    static const char *const q[] = {
        "Talk is cheap. Show me the code.",
        "There is no place like 127.0.0.1.",
        "It works on my machine. Ship the machine.",
        "A kernel panic a day keeps the boredom away.",
        "First, solve the problem. Then, write the code.",
        "Simplicity is prerequisite for reliability.",
        "Premature optimization is the root of all evil.",
        "The best code is no code at all.",
    };
    term_cputs(q[(uptime_s * 7 + t_min + t_hour) % (sizeof(q) / sizeof(q[0]))], TC_YELLOW);
    term_putc('\n');
}

/* ---------- оформление ---------- */

static void cmd_accent(char *args)
{
    char *p = args, *a = next_tok(&p);
    if (a) {
        for (int i = 0; i < ACCENT_COUNT; i++)
            if (s_eq_i(a, theme_accent_name(i))) {
                theme_set_accent(i);
                term_puts("accent: ");
                term_puts(theme_accent_name(i));
                term_putc('\n');
                return;
            }
    }
    term_puts("usage: accent");
    for (int i = 0; i < ACCENT_COUNT; i++) { term_putc(' '); term_puts(theme_accent_name(i)); }
    term_putc('\n');
}

static void cmd_dock(char *args)
{
    char *p = args, *a = next_tok(&p), *b = next_tok(&p);
    int v;
    if (a && s_eq(a, "hide") && b) {
        int m = s_eq(b, "off") ? 0 : s_eq(b, "auto") ? 1 : s_eq(b, "smart") ? 2 : -1;
        if (m >= 0) { wm_set_option(WM_OPT_DOCK_HIDE, m); term_puts("dock hide: "); term_puts(b); term_putc('\n'); return; }
    } else if (a && s_eq(a, "zoom") && b && s_atoi(b, &v) == 0 && v >= 0 && v <= 100) {
        wm_set_option(WM_OPT_DOCK_ZOOM, v);
        term_puts("dock zoom: "); term_puts(b); term_putc('\n');
        return;
    } else if (a && s_eq(a, "magnify") && b && (s_eq(b, "on") || s_eq(b, "off"))) {
        wm_set_option(WM_OPT_DOCK_MAGNIFY, s_eq(b, "on"));
        term_puts("dock magnify: "); term_puts(b); term_putc('\n');
        return;
    }
    term_puts("usage: dock hide off|auto|smart   dock zoom 0-100   dock magnify on|off\n");
}

/* ---------- сеть ---------- */

static int nj_wait;                                /* терминал ждёт завершения ping/nslookup */

static int nj_dirty;
static void nj_log(const char *line) { term_puts(line); term_putc('\n'); nj_dirty = 1; }

static void cmd_ifconfig(void)
{
    net_info_t i;
    char b[24];
    net_info(&i);
    if (!i.present) { term_puts("ifconfig: no network adapter found\n"); return; }
    term_cputs("en0", TC_ACCENT); term_puts(i.up ? ": flags=UP,RUNNING\n" : ": flags=UP (waiting for DHCP)\n");
    term_puts("    ether ");
    for (int k = 0; k < 6; k++) {
        static const char hx[] = "0123456789abcdef";
        term_putc(hx[i.mac[k] >> 4]); term_putc(hx[i.mac[k] & 15]);
        if (k < 5) term_putc(':');
    }
    term_putc('\n');
    if (!i.up) return;
    net_ip_fmt(i.ip, b);   term_puts("    inet "); term_puts(b);
    net_ip_fmt(i.mask, b); term_puts("  netmask "); term_puts(b); term_putc('\n');
    net_ip_fmt(i.gw, b);   term_puts("    gateway "); term_puts(b);
    net_ip_fmt(i.dns, b);  term_puts("  dns "); term_puts(b); term_putc('\n');
}

static void cmd_ping(char *arg)
{
    char *p = arg, *a = next_tok(&p);
    int count = 4;
    if (a && s_eq(a, "-c")) {
        char *n = next_tok(&p);
        count = 0;
        while (n && *n >= '0' && *n <= '9') count = count * 10 + (*n++ - '0');
        if (count < 1) count = 1;
        if (count > 100) count = 100;
        a = next_tok(&p);
    }
    if (!a) { term_puts("usage: ping [-c N] host\n"); return; }
    net_set_log(nj_log);
    net_ping(a, count);
}

static void cmd_nslookup(char *arg)
{
    char *p = arg, *a = next_tok(&p);
    if (!a) { term_puts("usage: nslookup host\n"); return; }
    net_set_log(nj_log);
    net_nslookup(a);
}

static void cmd_curl(char *arg)
{
    char *p = arg, *a = next_tok(&p);
    int hdr = 0;
    if (a && s_eq(a, "-i")) { hdr = 1; a = next_tok(&p); }
    if (!a) { term_puts("usage: curl [-i] http://host[:port]/path\n"); return; }
    net_set_log(nj_log);
    net_http_get(a, hdr);
}

/* ---------- справка ---------- */

typedef struct { const char *name; const char *usage; } cmd_info_t;
static const cmd_info_t cmd_table[] = {
    { "help", "help [command]" }, { "ls", "ls [-l] [path]" }, { "cd", "cd [path]" }, { "pwd", "pwd" },
    { "cat", "cat file..." }, { "mkdir", "mkdir name..." }, { "touch", "touch name..." },
    { "rm", "rm [-r] path..." }, { "mv", "mv source destination" }, { "cp", "cp source destination" },
    { "echo", "echo text [> file | >> file]" }, { "tree", "tree [path]" }, { "find", "find name" },
    { "grep", "grep [-i] [-n] text [file...]  (or: cmd | grep text)" }, { "head", "head [-n N] [file]" },
    { "tail", "tail [-n N] [file]" }, { "wc", "wc [-l|-w|-c] [file]" }, { "sort", "sort [-r] [file]" },
    { "neofetch", "neofetch" }, { "uname", "uname [-a]" }, { "uptime", "uptime" }, { "date", "date" },
    { "cal", "cal [month year]" }, { "calc", "calc 2+2*(3-1)" }, { "cpu", "cpu" }, { "mem", "mem" },
    { "free", "free" }, { "df", "df" }, { "ps", "ps" }, { "top", "top" }, { "kill", "kill name|pid" },
    { "whoami", "whoami" }, { "hostname", "hostname" }, { "sw_vers", "sw_vers" }, { "history", "history" },
    { "open", "open files|terminal|settings|snake|about|launchpad|mission" }, { "snake", "snake" },
    { "ifconfig", "ifconfig" }, { "ping", "ping [-c N] host  (Ctrl+C stops)" }, { "nslookup", "nslookup host" }, { "curl", "curl [-i] http://host/path  (Ctrl+C stops; no HTTPS yet)" },
    { "files", "files" }, { "wallpaper", "wallpaper N" }, { "theme", "theme light|dark" },
    { "accent", "accent purple|blue|green|..." }, { "dock", "dock hide off|auto|smart | zoom N | magnify on|off" },
    { "notify", "notify text" }, { "spotlight", "spotlight" }, { "cmatrix", "cmatrix (q to quit)" }, { "cowsay", "cowsay text" }, { "fortune", "fortune" },
    { "clear", "clear  (or Ctrl+L)" }, { "exit", "exit" }, { "reboot", "reboot" },
    { "shutdown", "shutdown" }, { "poweroff", "poweroff" },
};
#define CMD_COUNT ((int)(sizeof(cmd_table) / sizeof(cmd_table[0])))

static void cmd_help(char *args)
{
    char *p = args, *a = next_tok(&p);
    if (a) {
        for (int i = 0; i < CMD_COUNT; i++)
            if (s_eq(a, cmd_table[i].name)) {
                term_cputs("usage: ", TC_GRAY);
                term_puts(cmd_table[i].usage);
                term_putc('\n');
                return;
            }
        term_puts("help: no help for: ");
        term_puts(a);
        term_putc('\n');
        return;
    }
    term_cputs("EndixOS shell", TC_ACCENT);
    term_cputs("  (help <command> for usage)\n", TC_GRAY);
    term_cputs("Files   ", TC_YELLOW); term_puts("ls cd pwd cat mkdir touch rm mv cp tree find\n");
    term_cputs("Text    ", TC_YELLOW); term_puts("echo grep head tail wc sort   pipes: a | b   redirect: > >>\n");
    term_cputs("System  ", TC_YELLOW); term_puts("neofetch uname uptime date cal calc cpu mem df ps kill history\n");
    term_cputs("Apps    ", TC_YELLOW); term_puts("open snake files   (open launchpad, open mission)\n");
    term_cputs("Network ", TC_YELLOW); term_puts("ifconfig ping nslookup curl\n");
    term_cputs("Look    ", TC_YELLOW); term_puts("wallpaper theme accent dock\n");
    term_cputs("Fun     ", TC_YELLOW); term_puts("cmatrix cowsay fortune\n");
    term_cputs("Power   ", TC_YELLOW); term_puts("clear exit reboot shutdown\n");
    term_cputs("Keys    ", TC_YELLOW); term_puts("Up/Down history, Tab complete, PgUp/PgDn scroll, Ctrl+C/L/U\n");
}

static void term_hist_print(void);

/* Возвращает 1, если команда обработана. */
static int term_ext_cmd(const char *cmd, char *arg)
{
    if (s_eq(cmd, "help")) cmd_help(arg);
    else if (s_eq(cmd, "grep")) cmd_grep(arg);
    else if (s_eq(cmd, "head")) cmd_head_tail(0, arg);
    else if (s_eq(cmd, "tail")) cmd_head_tail(1, arg);
    else if (s_eq(cmd, "wc")) cmd_wc(arg);
    else if (s_eq(cmd, "sort")) cmd_sort(arg);
    else if (s_eq(cmd, "tree")) {
        char *p = arg, *a = next_tok(&p);
        int node = a ? fs_resolve(cwd, a) : cwd;
        if (node < 0) term_err("tree", a, FS_ENOENT);
        else if (!fs_is_dir(node)) { term_puts(fs_name(node)); term_putc('\n'); }
        else {
            char prefix[48];
            prefix[0] = 0;
            term_cputs(a ? a : ".", TC_BLUE);
            term_putc('\n');
            tree_walk(node, 0, prefix);
        }
    } else if (s_eq(cmd, "find")) {
        char *p = arg, *a = next_tok(&p);
        if (!a) term_puts("usage: find name\n");
        else { int found = 0; find_walk(cwd, a, &found); if (!found) term_puts("find: nothing found\n"); }
    }
    else if (s_eq(cmd, "cal")) cmd_cal(arg);
    else if (s_eq(cmd, "calc") || s_eq(cmd, "expr")) cmd_calc(arg);
    else if (s_eq(cmd, "ps") || s_eq(cmd, "top")) cmd_ps();
    else if (s_eq(cmd, "kill") || s_eq(cmd, "killall")) cmd_kill(arg);
    else if (s_eq(cmd, "open")) cmd_open(arg);
    else if (s_eq(cmd, "ifconfig")) cmd_ifconfig();
    else if (s_eq(cmd, "ping")) cmd_ping(arg);
    else if (s_eq(cmd, "nslookup")) cmd_nslookup(arg);
    else if (s_eq(cmd, "curl") || s_eq(cmd, "wget")) cmd_curl(arg);
    else if (s_eq(cmd, "df")) cmd_df();
    else if (s_eq(cmd, "neofetch")) cmd_neofetch();
    else if (s_eq(cmd, "cowsay")) cmd_cowsay(arg);
    else if (s_eq(cmd, "fortune")) cmd_fortune();
    else if (s_eq(cmd, "accent")) cmd_accent(arg);
    else if (s_eq(cmd, "dock")) cmd_dock(arg);
    else if (s_eq(cmd, "notify")) { if (*arg) notify("Terminal", arg); else term_puts("usage: notify text\n"); }
    else if (s_eq(cmd, "spotlight")) sp_toggle();
    else if (s_eq(cmd, "history")) term_hist_print();
    else if (s_eq(cmd, "whoami")) term_puts("endix\n");
    else if (s_eq(cmd, "hostname")) term_puts("endix-v8\n");
    else if (s_eq(cmd, "sw_vers")) {
        term_puts("ProductName:    EndixOS\nProductVersion: " ENDIX_VERSION "\nBuildVersion:   i686\n");
    } else if (s_eq(cmd, "free")) {
        char b[16];
        term_cputs("RAM total: ", TC_GRAY); u_put(b, sys.ram_mb); term_puts(b); term_puts(" MB\n");
        term_cputs("FS used:   ", TC_GRAY); u_put(b, fs_used_bytes() / 1024); term_puts(b); term_puts(" KB\n");
    } else if (s_eq(cmd, "exit")) close_window(APP_TERMINAL);
    else if (s_eq(cmd, "shutdown") || s_eq(cmd, "poweroff") || s_eq(cmd, "halt")) shutdown_now();
    else if (s_eq(cmd, "restart")) sys_reboot();
    else if (s_eq(cmd, "mission")) mc_toggle();
    else if (s_eq(cmd, "launchpad")) lp_show();
    else return 0;
    return 1;
}
