/* Хост-тест интеграции: Files, Snake и файловые команды терминала через события окон.
 * wm.c включён целиком, чтобы видеть его статики (tbuf, wins, ...). */
#include "gui/wm.c"
static void settle(void) { u32 s0 = now_ticks; for (int i = 1; i <= 40; i++) wm_tick(s0 + 2 * i); wm_tick(s0); }

typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *);
extern unsigned fwrite(const void *, unsigned, unsigned, FILE *);
extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long);
extern int printf(const char *, ...);
extern int snprintf(char *, unsigned long, const char *, ...);
extern char *strstr(const char *, const char *);
extern int strcmp(const char *, const char *);
extern int memcmp(const void *, const void *, unsigned long);

void sys_reboot(void) {} void sys_shutdown(void) {}

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("PASS  %s\n", msg); \
    else { printf("FAIL  %s\n", msg); fails++; } } while (0)

static u32 *vram;
static const char *prefix;
static u32 T = 5000;

static void frame(const char *name)
{
    char path[256];
    wm_render();
    fb_flip();
    snprintf(path, sizeof path, "%s_%s.raw", prefix, name);
    FILE *f = fopen(path, "wb");
    fwrite(vram, 4, 1024 * 768, f);
    fclose(f);
}

static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); settle(); }
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); }
static void key(i32 c) { event_t e = { EV_KEY, c, 0, 0 }; wm_handle_event(&e); settle(); }
static void ticks(int n) { for (int i = 0; i < n; i++) wm_tick(T += 14); }

static void type(const char *s)
{
    for (; *s; s++) key((u8)*s);
    key('\n');
}

static void run(const char *cmd)                         /* чистый экран терминала, затем команда */
{
    type("clear");
    type(cmd);
}

static int term_has(const char *needle)
{
    char row[TCOLS + 1];
    for (int r = 0; r < TROWS; r++) {
        memcpy(row, tbuf[r], TCOLS);
        row[TCOLS] = 0;
        if (strstr(row, needle)) return 1;
    }
    return 0;
}

/* Dock: центры иконок */
static void dock(int app) { mouse(512, 767, 0); for (int i = 0; i < 60; i++) wm_tick(T += 2); i32 ix, iy; dock_icon_pos(app, &ix, &iy); mouse(ix + 26, iy + 26, 0); dock_icon_pos(app, &ix, &iy); click(ix + 20, iy + 20); }

/* Клик в области содержимого окна Files */
static void fclick(i32 rx, i32 ry)
{
    click(wins[APP_FILES].x + rx, wins[APP_FILES].y + TITLE_H + ry);
}

/* Центр элемента сетки Files (4 колонки) */
static void fitem(int idx, i32 *rx, i32 *ry)
{
    *rx = 167 + (idx % 4) * 84 + 42;
    *ry = 78 + (idx / 4) * 92 + 30;
}

static void fitem_click(int idx)
{
    i32 rx, ry;
    fitem(idx, &rx, &ry);
    fclick(rx, ry);
}

static void fitem_dblclick(int idx)
{
    wm_tick(T += 100);
    fitem_click(idx);
    wm_tick(T += 10);
    fitem_click(idx);
}

static void fuzz(void);

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    prefix = argv[1];
    vram = mmap(0, 1024 * 768 * 4, 3, 0x62, -1, 0);
    if (vram == (void *)-1 || fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32)) return 2;

    static sysinfo_t info = { "Intel(R) Core(TM) i7-12700 CPU @ 2.10GHz", 128, 1024, 768 };
    wm_init(&info);
    wm_set_time(10, 31, 24, 10, 2026);

    int home = fs_home();
    int docs = fs_resolve(0, "/home/endix/Documents");

    /* ---------- Files ---------- */
    dock(APP_FILES);
    CHECK(wins[APP_FILES].open && top_app() == APP_FILES, "Dock открывает Files");
    CHECK(wins[APP_FILES].y + wins[APP_FILES].h <= 768 - 80, "окно Files целиком на экране");
    frame("f0_home");

    fitem_dblclick(1);                                   /* Documents */
    frame("f1_documents");

    fclick(162 + 40, 43 + 10);                           /* New Folder */
    CHECK(fs_lookup(docs, "untitled folder") >= 0, "New Folder создала папку в Documents (значит, двойной клик открыл её)");
    frame("f2_rename");
    type("Projects");
    CHECK(fs_lookup(docs, "Projects") >= 0 && fs_lookup(docs, "untitled folder") < 0,
          "переименование: набор текста заменяет имя");

    fclick(162 + 96 + 8 + 30, 43 + 10);                  /* New File */
    CHECK(fs_lookup(docs, "untitled.txt") >= 0, "New File создал untitled.txt");
    key(27);
    CHECK(fs_lookup(docs, "untitled.txt") >= 0, "Esc отменяет переименование, файл остаётся");

    fitem_click(2);                                      /* readme.txt */
    frame("f3_preview");
    key(KEY_F2);
    type("README.md");
    CHECK(fs_resolve(docs, "README.md") >= 0 && fs_lookup(docs, "readme.txt") < 0, "F2: переименование файла");
    int rm = fs_lookup(docs, "README.md");
    CHECK(rm >= 0 && fs_size(rm) > 100, "переименованный файл сохранил содержимое");

    fclick(162, 43 + 10);                                /* New Folder + дубликат имени */
    type("Projects");
    CHECK(fs_lookup(docs, "untitled folder") >= 0, "занятое имя не принято, поле осталось");
    frame("f4_name_taken");
    key(27);

    key(KEY_DELETE);
    CHECK(fs_lookup(docs, "untitled folder") < 0, "Delete удаляет выбранную папку");

    /* удаление выбранного */
    fitem_click(2);                                      /* README.md */
    key(KEY_DELETE);
    CHECK(fs_lookup(docs, "README.md") < 0, "Delete удаляет выбранный файл");
    frame("f5_deleted");

    key('\b');                                           /* Backspace: наверх */
    fitem_click(0);                                      /* Desktop */
    key('\n');                                           /* Enter открывает папку */
    fclick(162, 43 + 10);
    type("fromfiles");
    CHECK(fs_resolve(0, "/home/endix/Desktop/fromfiles") >= 0, "Backspace, выбор и Enter: перешли в Desktop");

    fclick(175, 19);                                     /* кнопка Back */
    fclick(162, 43 + 10);
    type("afterback");
    CHECK(fs_lookup(home, "afterback") >= 0, "Back вернул в домашнюю папку");

    fclick(75, 34 + 3 * 30 + 13);                        /* боковая панель: Downloads */
    fclick(162, 43 + 10);
    type("indownloads");
    CHECK(fs_resolve(0, "/home/endix/Downloads/indownloads") >= 0, "боковая панель ведёт в Downloads");

    for (int i = 0; i < 6; i++) key(KEY_RIGHT);          /* навигация стрелками не падает */
    frame("f6_arrows");

    /* ---------- Terminal + файловые команды ---------- */
    dock(APP_TERMINAL);
    CHECK(top_app() == APP_TERMINAL, "Dock поднимает Terminal");
    type("clear");
    type("ls");
    CHECK(term_has("Documents/") && term_has("Desktop/"), "ls показывает папки со слэшем");
    type("cd Documents");
    run("pwd");
    CHECK(term_has("/home/endix/Documents"), "pwd");
    CHECK(term_has("~/Documents %"), "приглашение показывает ~/Documents");
    type("cat notes.txt");
    CHECK(term_has("a real disk driver"), "cat читает файл");
    type("echo hello > hi.txt");
    int hi = fs_resolve(docs, "hi.txt");
    CHECK(hi >= 0 && fs_size(hi) == 6 && memcmp(fs_data(hi), "hello\n", 6) == 0, "echo > создаёт файл");
    type("echo more >> hi.txt");
    CHECK(fs_size(hi) == 11 && memcmp(fs_data(hi), "hello\nmore\n", 11) == 0, "echo >> дописывает");
    type("echo again > hi.txt");
    CHECK(fs_size(hi) == 6 && memcmp(fs_data(hi), "again\n", 6) == 0, "echo > перезаписывает");
    type("ls -l");
    CHECK(term_has("6  hi.txt"), "ls -l показывает размер");
    type("mkdir demo");
    type("mv hi.txt demo");
    CHECK(fs_resolve(docs, "demo/hi.txt") >= 0 && fs_lookup(docs, "hi.txt") < 0, "mv переносит в папку");
    type("cp notes.txt demo/n2.txt");
    int n1 = fs_lookup(docs, "notes.txt"), n2 = fs_resolve(docs, "demo/n2.txt");
    CHECK(n2 >= 0 && fs_size(n1) == fs_size(n2) && memcmp(fs_data(n1), fs_data(n2), fs_size(n1)) == 0,
          "cp копирует файл с содержимым");
    type("mv demo/n2.txt demo/n3.txt");
    CHECK(fs_resolve(docs, "demo/n3.txt") >= 0 && fs_resolve(docs, "demo/n2.txt") < 0, "mv переименовывает");
    type("rm demo");
    CHECK(term_has("is a directory") && fs_lookup(docs, "demo") >= 0, "rm без -r не трогает папку");
    type("rm -r demo");
    CHECK(fs_lookup(docs, "demo") < 0, "rm -r удаляет папку с содержимым");
    type("cd /etc");
    type("cat hostname");
    CHECK(term_has("endix"), "cd /etc, cat hostname");
    type("cd nope");
    CHECK(term_has("no such file"), "cd в несуществующее: ошибка");
    type("rm -r /");
    CHECK(term_has("not permitted") && fs_is_dir(0), "корень защищён от rm -r");
    type("mkdir a/b/c");
    CHECK(term_has("no such file"), "mkdir в несуществующей папке: ошибка");
    type("echo x > /nope/f");
    CHECK(term_has("no such file"), "echo > в несуществующую папку: ошибка");
    type("cd ~");
    type("touch a.txt b.txt");
    CHECK(fs_lookup(home, "a.txt") >= 0 && fs_lookup(home, "b.txt") >= 0, "touch создаёт несколько файлов");
    type("touch a.txt");
    CHECK(fs_lookup(home, "a.txt") >= 0, "touch существующего не ломает");
    type("cat");
    CHECK(term_has("usage: cat"), "cat без аргумента: подсказка");
    type("help");
    CHECK(term_has("snake"), "help знает snake");
    frame("t0_terminal");

    /* Files видит изменения терминала */
    dock(APP_FILES);
    frame("t1_files_sees_terminal");

    /* папка терминала исчезла из-за Files: приглашение возвращается домой */
    dock(APP_TERMINAL);
    type("cd Downloads");
    dock(APP_FILES);
    fclick(75, 34 + 13);                                 /* Home */
    fitem_click(3);                                      /* Downloads (первой идёт папка afterback) */
    key(KEY_DELETE);
    dock(APP_TERMINAL);
    run("pwd");
    CHECK(term_has(".Trash/Downloads"), "удалённая из Files папка уходит в Корзину");

    /* ---------- Snake ---------- */
    dock(APP_SNAKE);
    CHECK(top_app() == APP_SNAKE, "Dock открывает Snake");
    frame("s0_ready");
    key(KEY_UP);
    ticks(4);
    frame("s1_playing");
    dock(APP_TERMINAL);
    ticks(2);
    dock(APP_SNAKE);
    frame("s2_autopaused");
    key(' ');
    ticks(3);
    key('r');
    ticks(40);                                           /* змея упирается в стену */
    frame("s3_over");

    /* тёмная тема */
    dock(APP_TERMINAL);
    type("theme dark");
    dock(APP_FILES);
    frame("d0_files_dark");
    dock(APP_TERMINAL);
    type("theme light");

    fuzz();

    printf("\nfails=%d\n", fails);
    return fails ? 1 : 0;
}

/* ---------- случайные события: не должно быть падений и выхода за границы ---------- */

static u8 seen[FS_MAX_NODES];

static int visit(int dir, int depth)
{
    int list[FS_MAX_NODES];
    int n = fs_list(dir, list, FS_MAX_NODES), total = n;
    if (depth > 20)
        return -1000;
    for (int i = 0; i < n; i++) {
        char p[200];
        if (seen[list[i]])
            return -1000;                                /* узел встретился дважды: цикл */
        seen[list[i]] = 1;
        fs_path(list[i], p, sizeof p);
        if (fs_resolve(0, p) != list[i])
            return -1000;
        if (fs_is_dir(list[i])) {
            int sub = visit(list[i], depth + 1);
            if (sub < 0)
                return sub;
            total += sub;
        }
    }
    return total;
}

static u32 rs = 88172645u;
static u32 rnd32(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static void fuzz(void)
{
    static const char *const cmds[] = {
        "ls", "ls -l /", "cd ..", "cd ~", "cd /", "mkdir x", "mkdir x/y", "rm -r x", "touch f", "echo a > f",
        "echo b >> f", "cat f", "mv f g", "cp g h", "rm g h", "snake", "files", "theme dark", "theme light",
        "wallpaper 3", "wallpaper 1", "pwd", "clear", "cat /etc/os-release", "rm -r /home", "mv /home /tmp/h",
        "cp f f", "mkdir /tmp/a", "cd /tmp/a", "echo > ", ">>", "ls /nonexistent",
    };
    static const i32 specials[] = { '\n', '\b', 27, ' ', KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
                                    KEY_DELETE, KEY_F2, 'r', 'w', 'a', 's', 'd', '/', '.' };
    u32 buttons = 0;

    for (int i = 0; i < 60000; i++) {
        u32 r = rnd32() % 100;
        if (r < 55) {
            i32 x = (i32)(rnd32() % 1024), y = (i32)(rnd32() % 768);
            if (rnd32() % 4 == 0) buttons ^= 1;
            mouse(x, y, (i32)buttons);
            if (buttons && rnd32() % 3 == 0) { buttons = 0; mouse(x, y, 0); }
        } else if (r < 80) {
            key(specials[rnd32() % (sizeof(specials) / sizeof(specials[0]))]);
        } else if (r < 90) {
            key((i32)(32 + rnd32() % 95));
        } else if (r < 94) {
            dock((int)(rnd32() % 5));
        } else if (r < 98) {
            dock(APP_TERMINAL);
            type(cmds[rnd32() % (sizeof(cmds) / sizeof(cmds[0]))]);
        } else {
            ticks(1 + (int)(rnd32() % 5));
        }
        if (i % 40 == 0) { wm_render(); fb_flip(); }
    }
    buttons = 0;
    mouse(500, 400, 0);
    wm_render();
    fb_flip();

    /* дерево файлов осталось связным: каждый живой узел достижим от корня ровно один раз,
     * а путь к нему снова приводит в тот же узел */
    int reachable = visit(0, 0), live = 0, ok = (reachable >= 0);
    for (int i = 1; i < FS_MAX_NODES; i++) {
        if (fs_valid(i)) live++;
        if (fs_valid(i) != seen[i]) ok = 0;
    }
    CHECK(ok && reachable == live, "после 60000 случайных событий дерево файлов цело");
    printf("      (узлов в ФС: %d, занято данных: %u байт)\n", live, fs_used_bytes());
}
