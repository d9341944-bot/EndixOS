/* Файловая система в памяти. Данные файлов лежат в общем пуле, который
 * при нехватке места уплотняется. */
#include "fs.h"
#include "string.h"

typedef struct {
    char name[FS_NAME_MAX];
    i16  parent;
    u8   used;
    u8   is_dir;
    u32  size;        /* длина данных */
    u32  cap;         /* сколько места выделено в пуле */
    u32  off;         /* где лежат данные */
} node_t;

static node_t nodes[FS_MAX_NODES];
static u8     pool[FS_POOL_SIZE];
static u32    pool_top;
static int    home_node;

/* ---------- строки ---------- */

static int s_len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int s_icmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return (u8)lower(*a) - (u8)lower(*b);
}

static void s_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* ---------- пул данных ---------- */

/* Сдвигает данные всех файлов к началу пула, выкидывая дырки от удалённых и переехавших. */
static void compact(void)
{
    static u8 done[FS_MAX_NODES];
    u32 cursor = 0;

    memset(done, 0, sizeof(done));
    for (;;) {
        int best = -1;
        for (int i = 0; i < FS_MAX_NODES; i++) {
            if (!nodes[i].used || nodes[i].is_dir || nodes[i].cap == 0 || done[i])
                continue;
            if (best < 0 || nodes[i].off < nodes[best].off)
                best = i;
        }
        if (best < 0)
            break;
        done[best] = 1;
        if (nodes[best].off != cursor)
            memmove(&pool[cursor], &pool[nodes[best].off], nodes[best].size);
        nodes[best].off = cursor;
        cursor += nodes[best].cap;
    }
    pool_top = cursor;
}

static int pool_alloc(u32 need, u32 *off)
{
    if (pool_top + need > FS_POOL_SIZE)
        compact();
    if (pool_top + need > FS_POOL_SIZE)
        return -1;
    *off = pool_top;
    pool_top += need;
    return 0;
}

u32 fs_used_bytes(void)
{
    u32 total = 0;
    for (int i = 0; i < FS_MAX_NODES; i++)
        if (nodes[i].used && !nodes[i].is_dir)
            total += nodes[i].size;
    return total;
}

/* ---------- узлы ---------- */

int fs_root(void) { return 0; }
int fs_home(void) { return home_node; }

int fs_valid(int n)
{
    return n >= 0 && n < FS_MAX_NODES && nodes[n].used;
}

int fs_is_dir(int n)
{
    return fs_valid(n) && nodes[n].is_dir;
}

const char *fs_name(int n)
{
    return fs_valid(n) ? nodes[n].name : "";
}

int fs_parent(int n)
{
    return fs_valid(n) ? nodes[n].parent : -1;
}

u32 fs_size(int n)
{
    return fs_valid(n) && !nodes[n].is_dir ? nodes[n].size : 0;
}

const u8 *fs_data(int n)
{
    if (!fs_valid(n) || nodes[n].is_dir || nodes[n].size == 0)
        return (const u8 *)"";
    return &pool[nodes[n].off];
}

int fs_lookup(int dir, const char *name)
{
    if (!fs_is_dir(dir))
        return -1;
    for (int i = 1; i < FS_MAX_NODES; i++)
        if (nodes[i].used && nodes[i].parent == dir && s_eq(nodes[i].name, name))
            return i;
    return -1;
}

int fs_resolve(int cwd, const char *path)
{
    int cur = cwd;
    char comp[FS_NAME_MAX];

    if (*path == '/')
        cur = 0;
    else if (path[0] == '~' && (path[1] == 0 || path[1] == '/')) {
        cur = home_node;
        path++;
    }

    while (*path) {
        while (*path == '/')
            path++;
        if (!*path)
            break;

        int n = 0;
        while (*path && *path != '/') {
            if (n >= FS_NAME_MAX - 1)
                return -1;
            comp[n++] = *path++;
        }
        comp[n] = 0;

        if (s_eq(comp, "."))
            continue;
        if (s_eq(comp, "..")) {
            if (nodes[cur].parent >= 0)
                cur = nodes[cur].parent;
            continue;
        }
        cur = fs_lookup(cur, comp);
        if (cur < 0)
            return -1;
    }
    return cur;
}

void fs_path(int node, char *buf, int max)
{
    int chain[24], depth = 0;
    int n = node;

    while (fs_valid(n) && n != 0 && depth < 24) {
        chain[depth++] = n;
        n = nodes[n].parent;
    }

    int p = 0;
    if (depth == 0) {
        if (max > 1) {
            buf[0] = '/';
            buf[1] = 0;
        }
        return;
    }
    while (depth-- > 0) {
        if (p < max - 1)
            buf[p++] = '/';
        for (const char *s = nodes[chain[depth]].name; *s && p < max - 1; s++)
            buf[p++] = *s;
    }
    buf[p] = 0;
}

/* ---------- содержимое папки ---------- */

static int before(int a, int b)       /* a должен идти раньше b */
{
    if (nodes[a].is_dir != nodes[b].is_dir)
        return nodes[a].is_dir;
    return s_icmp(nodes[a].name, nodes[b].name) < 0;
}

int fs_list(int dir, int *out, int max)
{
    int n = 0;
    if (!fs_is_dir(dir))
        return 0;
    for (int i = 1; i < FS_MAX_NODES && n < max; i++) {
        if (!nodes[i].used || nodes[i].parent != dir)
            continue;
        int j = n++;
        while (j > 0 && before(i, out[j - 1])) {     /* вставка с сохранением порядка */
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
    }
    return n;
}

int fs_count(int dir)
{
    int n = 0;
    if (!fs_is_dir(dir))
        return 0;
    for (int i = 1; i < FS_MAX_NODES; i++)
        if (nodes[i].used && nodes[i].parent == dir)
            n++;
    return n;
}

/* ---------- создание, запись, удаление ---------- */

static int valid_name(const char *name)
{
    int n = s_len(name);
    if (n == 0 || n >= FS_NAME_MAX)
        return 0;
    if (s_eq(name, ".") || s_eq(name, ".."))
        return 0;
    for (int i = 0; i < n; i++)
        if (name[i] == '/' || (u8)name[i] < 32)
            return 0;
    return 1;
}

static int new_node(int parent, const char *name, int is_dir)
{
    if (!fs_is_dir(parent) || !valid_name(name))
        return FS_EINVAL;
    if (fs_lookup(parent, name) >= 0)
        return FS_EEXIST;
    for (int i = 1; i < FS_MAX_NODES; i++) {
        if (nodes[i].used)
            continue;
        memset(&nodes[i], 0, sizeof(node_t));
        s_copy(nodes[i].name, name, FS_NAME_MAX);
        nodes[i].parent = (i16)parent;
        nodes[i].used = 1;
        nodes[i].is_dir = (u8)is_dir;
        return i;
    }
    return FS_ENOSPC;
}

int fs_mkdir(int parent, const char *name)  { return new_node(parent, name, 1); }
int fs_create(int parent, const char *name) { return new_node(parent, name, 0); }

int fs_write(int n, const char *data, u32 len, int append)
{
    if (!fs_valid(n) || nodes[n].is_dir)
        return FS_EINVAL;

    node_t *nd = &nodes[n];
    u32 keep = append ? nd->size : 0;
    u32 total = keep + len;
    if (total > FS_FILE_MAX)
        return FS_ENOSPC;

    if (total > nd->cap) {
        u32 newcap = (total + 63) & ~63u;           /* с запасом на дозапись */
        u32 off;
        if (pool_alloc(newcap, &off) < 0)           /* пул мог уплотниться: nd->off актуален */
            return FS_ENOSPC;
        if (keep)
            memcpy(&pool[off], &pool[nd->off], keep);
        nd->off = off;
        nd->cap = newcap;
    }
    if (len)
        memcpy(&pool[nd->off + keep], data, len);
    nd->size = total;
    return 0;
}

static int protected_node(int n)
{
    return n == 0 || n == home_node;
}

int fs_remove(int n)
{
    if (!fs_valid(n))
        return FS_ENOENT;
    if (protected_node(n))
        return FS_EPERM;
    if (nodes[n].is_dir && fs_count(n) > 0)
        return FS_ENOTEMPTY;
    nodes[n].used = 0;
    nodes[n].cap = 0;
    return 0;
}

int fs_remove_tree(int n)
{
    if (!fs_valid(n))
        return FS_ENOENT;
    if (protected_node(n))
        return FS_EPERM;
    if (nodes[n].is_dir) {
        for (int i = 1; i < FS_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == n)
                fs_remove_tree(i);
    }
    return fs_remove(n);
}

int fs_move(int n, int new_parent, const char *new_name)
{
    if (!fs_valid(n))
        return FS_ENOENT;
    if (protected_node(n))
        return FS_EPERM;
    if (!fs_is_dir(new_parent) || !valid_name(new_name))
        return FS_EINVAL;

    int same = (nodes[n].parent == new_parent && s_eq(nodes[n].name, new_name));
    if (!same && fs_lookup(new_parent, new_name) >= 0)
        return FS_EEXIST;

    if (nodes[n].is_dir) {                           /* нельзя переносить папку внутрь себя */
        for (int p = new_parent; p >= 0; p = nodes[p].parent)
            if (p == n)
                return FS_EINVAL;
    }
    nodes[n].parent = (i16)new_parent;
    s_copy(nodes[n].name, new_name, FS_NAME_MAX);
    return 0;
}

void fs_unique_name(int parent, const char *base, char *out)
{
    char stem[FS_NAME_MAX], ext[FS_NAME_MAX];
    int len = s_len(base), dot = -1;

    for (int i = len - 1; i > 0; i--) {
        if (base[i] == '.') {
            dot = i;
            break;
        }
    }
    if (dot < 0) {
        s_copy(stem, base, FS_NAME_MAX);
        ext[0] = 0;
    } else {
        int n = dot < FS_NAME_MAX - 1 ? dot : FS_NAME_MAX - 1;
        for (int i = 0; i < n; i++)
            stem[i] = base[i];
        stem[n] = 0;
        s_copy(ext, base + dot, FS_NAME_MAX);
    }

    s_copy(out, base, FS_NAME_MAX);
    for (int k = 2; fs_lookup(parent, out) >= 0 && k < 1000; k++) {
        char num[8];
        int nl = 0, v = k;
        char tmp[8];
        while (v) {
            tmp[nl++] = (char)('0' + v % 10);
            v /= 10;
        }
        int p = 0;
        num[p++] = ' ';
        while (nl)
            num[p++] = tmp[--nl];
        num[p] = 0;

        int o = 0;
        for (int i = 0; stem[i] && o < FS_NAME_MAX - 1; i++)
            out[o++] = stem[i];
        for (int i = 0; num[i] && o < FS_NAME_MAX - 1; i++)
            out[o++] = num[i];
        for (int i = 0; ext[i] && o < FS_NAME_MAX - 1; i++)
            out[o++] = ext[i];
        out[o] = 0;
    }
}

/* ---------- стартовое содержимое ---------- */

static void put_file(int dir, const char *name, const char *text)
{
    int f = fs_create(dir, name);
    if (f >= 0)
        fs_write(f, text, (u32)s_len(text), 0);
}

void fs_init(void)
{
    memset(nodes, 0, sizeof(nodes));
    pool_top = 0;

    nodes[0].used = 1;                  /* корень */
    nodes[0].is_dir = 1;
    nodes[0].parent = -1;

    int home_dir = fs_mkdir(0, "home");
    int etc = fs_mkdir(0, "etc");
    fs_mkdir(0, "tmp");

    home_node = fs_mkdir(home_dir, "endix");
    int desktop   = fs_mkdir(home_node, "Desktop");
    int documents = fs_mkdir(home_node, "Documents");
    fs_mkdir(home_node, "Downloads");
    fs_mkdir(home_node, "Pictures");
    fs_mkdir(home_node, "Music");

    put_file(documents, "readme.txt",
        "Welcome to EndixOS 8!\n"
        "\n"
        "This is an operating system written from scratch:\n"
        "its own kernel, drivers and window system.\n"
        "\n"
        "Open the Terminal and try:\n"
        "  ls, cd, cat, mkdir, touch, rm, mv, echo hi > file\n"
        "\n"
        "Files you create there show up in the Files app.\n");
    put_file(documents, "notes.txt",
        "Ideas for EndixOS:\n"
        "- launchpad\n"
        "- more wallpapers\n"
        "- a real disk driver\n"
        "- sound\n");
    put_file(documents, "todo.txt",
        "[x] boot the kernel\n"
        "[x] mouse and keyboard\n"
        "[x] windows and Dock\n"
        "[x] Settings\n"
        "[x] Snake and Files\n"
        "[ ] Launchpad\n"
        "[ ] real disk\n");
    put_file(desktop, "hello.txt", "Hello from EndixOS!\n");
    put_file(etc, "os-release", "NAME=EndixOS\nVERSION=8.0\nARCH=i686\n");
    put_file(etc, "hostname", "endix\n");
}
