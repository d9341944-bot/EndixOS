/* Мини-libc для движка Doom (doomgeneric): без внешних зависимостей, без плавающей точки.
 * Память - своя арена, файлы - в оперативной памяти (WAD подкладывает doom_plat.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include "doom_sys.h"

int errno;

/* ---------- 64-битное деление (libgcc нет) ---------- */
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    if (d == 0) { if (rem) *rem = 0; return 0; }
    if ((n >> 32) == 0 && (d >> 32) == 0) { if (rem) *rem = (uint32_t)n % (uint32_t)d; return (uint32_t)n / (uint32_t)d; }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1ull << i; }
    }
    if (rem) *rem = r;
    return q;
}
uint64_t __udivdi3(uint64_t a, uint64_t b) { return __udivmoddi4(a, b, 0); }
uint64_t __umoddi3(uint64_t a, uint64_t b) { uint64_t r; __udivmoddi4(a, b, &r); return r; }
int64_t __divdi3(int64_t a, int64_t b)
{
    int neg = (a < 0) ^ (b < 0);
    uint64_t q = __udivmoddi4(a < 0 ? -(uint64_t)a : (uint64_t)a, b < 0 ? -(uint64_t)b : (uint64_t)b, 0);
    return neg ? -(int64_t)q : (int64_t)q;
}
int64_t __moddi3(int64_t a, int64_t b)
{
    uint64_t r; __udivmoddi4(a < 0 ? -(uint64_t)a : (uint64_t)a, b < 0 ? -(uint64_t)b : (uint64_t)b, &r);
    return a < 0 ? -(int64_t)r : (int64_t)r;
}

/* ---------- память: первая подходящая, со слиянием соседей ---------- */
typedef struct blk { uint32_t size; uint32_t used; struct blk *next; } blk;   /* size - вместе с заголовком; 16 байт */
#define HDR 16u
static uint8_t *arena_lo, *arena_hi;
static blk *first;

void doom_mem_init(void *base, uint32_t size)
{
    arena_lo = (uint8_t *)(((uintptr_t)base + 15) & ~15u);
    arena_hi = (uint8_t *)base + size;
    first = (blk *)arena_lo;
    first->size = (uint32_t)(arena_hi - arena_lo) & ~15u; first->used = 0; first->next = 0;
}

void *malloc(size_t n)
{
    if (!first) return 0;
    uint32_t need = ((uint32_t)n + 15u) & ~15u; need += HDR;
    for (blk *b = first; b; b = b->next) {
        if (b->used || b->size < need) continue;
        if (b->size >= need + HDR + 16) {
            blk *r = (blk *)((uint8_t *)b + need);
            r->size = b->size - need; r->used = 0; r->next = b->next;
            b->size = need; b->next = r;
        }
        b->used = 1;
        return (uint8_t *)b + HDR;
    }
    return 0;
}

void free(void *p)
{
    if (!p) return;
    blk *b = (blk *)((uint8_t *)p - HDR);
    b->used = 0;
    for (blk *c = first; c; c = c->next)                      /* слияние подряд идущих свободных */
        while (!c->used && c->next && !c->next->used) { c->size += c->next->size; c->next = c->next->next; }
}

void *calloc(size_t a, size_t b) { void *p = malloc(a * b); if (p) memset(p, 0, a * b); return p; }
void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    blk *b = (blk *)((uint8_t *)p - HDR);
    uint32_t have = b->size - HDR;
    if (n <= have) return p;
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, have); free(p);
    return q;
}

uint32_t doom_mem_free(void)
{
    uint32_t t = 0, big = 0;
    for (blk *b = first; b; b = b->next) if (!b->used) { t += b->size; if (b->size > big) big = b->size; }
    (void)t; return big;
}

/* ---------- строки ---------- */
int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (uint8_t)*a - (uint8_t)*b; }
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) { if (*a != *b) return (uint8_t)*a - (uint8_t)*b; if (!*a) return 0; }
    return 0;
}
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 32 && c < 127; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((uint8_t)*a) == tolower((uint8_t)*b)) { a++; b++; }
    return tolower((uint8_t)*a) - tolower((uint8_t)*b);
}
int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = tolower((uint8_t)*a), y = tolower((uint8_t)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}
char *strrchr(const char *s, int c)
{
    const char *r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
    return 0;
}
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

int abs(int x) { return x < 0 ? -x : x; }
long strtol(const char *s, char **e, int base)
{
    const char *p = s; int neg = 0; long v = 0;
    while (isspace((uint8_t)*p)) p++;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0) base = p[0] == '0' ? 8 : 10;
    for (;; p++) {
        int d = isdigit((uint8_t)*p) ? *p - '0' : isalpha((uint8_t)*p) ? tolower((uint8_t)*p) - 'a' + 10 : 99;
        if (d >= base) break;
        v = v * base + d;
    }
    if (e) *e = (char *)p;
    return neg ? -v : v;
}
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
int atof_unused;
int rand(void) { static uint32_t s = 12345; s = s * 1103515245u + 12345u; return (int)((s >> 16) & 0x7fff); }
void srand(unsigned s) { (void)s; }
char *getenv(const char *n) { (void)n; return 0; }
int system(const char *c) { (void)c; return -1; }
int access(const char *p, int m) { (void)p; (void)m; return -1; }
int mkdir(const char *p, int m) { (void)p; (void)m; return 0; }

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    uint8_t *a = base, tmp[64];
    if (sz > sizeof tmp) return;
    for (size_t gap = n / 2; gap > 0; gap /= 2)
        for (size_t i = gap; i < n; i++) {
            memcpy(tmp, a + i * sz, sz);
            size_t j = i;
            for (; j >= gap && cmp(a + (j - gap) * sz, tmp) > 0; j -= gap) memcpy(a + j * sz, a + (j - gap) * sz, sz);
            memcpy(a + j * sz, tmp, sz);
        }
}

/* ---------- printf ---------- */
typedef struct { char *b; size_t n, len; } outb;
static void put(outb *o, char c) { if (o->len + 1 < o->n) o->b[o->len] = c; o->len++; }

int vsnprintf(char *b, size_t n, const char *f, va_list ap)
{
    outb o = { b, n, 0 };
    for (; *f; f++) {
        if (*f != '%') { put(&o, *f); continue; }
        f++;
        int left = 0, zero = 0, plus = 0, w = 0, prec = -1, lng = 0;
        for (;; f++) { if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1; else if (*f == ' ' || *f == '#') { } else break; }
        if (*f == '*') { w = va_arg(ap, int); if (w < 0) { left = 1; w = -w; } f++; } else while (isdigit((uint8_t)*f)) w = w * 10 + (*f++ - '0');
        if (*f == '.') { f++; prec = 0; if (*f == '*') { prec = va_arg(ap, int); f++; } else while (isdigit((uint8_t)*f)) prec = prec * 10 + (*f++ - '0'); }
        while (*f == 'l' || *f == 'h' || *f == 'z') { if (*f == 'l' || *f == 'z') lng++; f++; }
        char tmp[24]; const char *s = tmp; int sl = 0, neg = 0;
        char c = *f;
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'p' || c == 'o') {
            uint32_t v; int base = 10;
            if (c == 'd' || c == 'i') { int x = va_arg(ap, int); neg = x < 0; v = neg ? -(uint32_t)x : (uint32_t)x; }
            else { v = va_arg(ap, uint32_t); if (c == 'x' || c == 'X' || c == 'p') base = 16; else if (c == 'o') base = 8; }
            char r[24]; int k = 0;
            do { int d = (int)(v % (uint32_t)base); r[k++] = (char)(d < 10 ? '0' + d : (c == 'X' ? 'A' : 'a') + d - 10); v /= (uint32_t)base; } while (v);
            if (prec >= 0) while (k < prec) r[k++] = '0';
            while (k) tmp[sl++] = r[--k];
            if (neg) { /* знак пишется ниже */ }
            else if (plus && (c == 'd' || c == 'i')) neg = 2;
            zero = zero && prec < 0 && !left;
        } else if (c == 's') {
            s = va_arg(ap, const char *); if (!s) s = "(null)";
            sl = (int)strlen(s); if (prec >= 0 && sl > prec) sl = prec;
            zero = 0;
        } else if (c == 'c') { tmp[0] = (char)va_arg(ap, int); sl = 1; zero = 0; }
        else if (c == '%') { tmp[0] = '%'; sl = 1; zero = 0; }
        else if (c == 'f' || c == 'g' || c == 'e') { (void)va_arg(ap, uint32_t); (void)va_arg(ap, uint32_t); tmp[0] = '0'; sl = 1; zero = 0; }   /* плавающей точки нет */
        else { if (!c) break; tmp[0] = c; sl = 1; }
        int total = sl + (neg ? 1 : 0);
        if (!left && !zero) for (int i = total; i < w; i++) put(&o, ' ');
        if (neg) put(&o, neg == 1 ? '-' : '+');
        if (!left && zero) for (int i = total; i < w; i++) put(&o, '0');
        for (int i = 0; i < sl; i++) put(&o, s[i]);
        if (left) for (int i = total; i < w; i++) put(&o, ' ');
    }
    if (n) b[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}
int snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, 4096, f, ap); va_end(ap); return r; }

/* ---------- sscanf: %d %i %u %x %o %s %c, пробелы и литералы ---------- */
int sscanf(const char *s, const char *f, ...)
{
    va_list ap; va_start(ap, f);
    int cnt = 0;
    for (; *f; f++) {
        if (isspace((uint8_t)*f)) { while (isspace((uint8_t)*s)) s++; continue; }
        if (*f != '%') { if (*s != *f) break; s++; continue; }
        f++;
        while (*f == 'l' || *f == 'h') f++;
        char c = *f;
        if (c == 'c') { char *p = va_arg(ap, char *); if (!*s) break; *p = *s++; cnt++; continue; }
        while (isspace((uint8_t)*s)) s++;
        if (c == 's') {
            char *p = va_arg(ap, char *); if (!*s) break;
            while (*s && !isspace((uint8_t)*s)) *p++ = *s++;
            *p = 0; cnt++; continue;
        }
        int base = c == 'x' || c == 'X' ? 16 : c == 'o' ? 8 : c == 'i' ? 0 : 10;
        char *e; long v = strtol(s, &e, base);
        if (e == s) break;
        *va_arg(ap, int *) = (int)v; s = e; cnt++;
    }
    va_end(ap);
    return cnt;
}

/* ---------- файлы: в оперативной памяти ---------- */
struct FILE { uint8_t *data; uint32_t size, cap, pos; int wr, console; };
typedef struct { char name[96]; uint8_t *data; uint32_t size, cap; int used, ro; } mfile;
#define NFILES 24
static mfile files[NFILES];
static FILE std_out = { 0, 0, 0, 0, 1, 1 }, std_err = { 0, 0, 0, 0, 1, 2 }, std_in;
FILE *stdout = &std_out, *stderr = &std_err, *stdin = &std_in;
static FILE ftab[8];

static const char *base_of(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/' || *p == '\\') b = p + 1; return b; }

void doom_file_add(const char *name, const void *data, uint32_t size)    /* только для чтения, данные не копируются */
{
    for (int i = 0; i < NFILES; i++) if (!files[i].used) {
        strncpy(files[i].name, base_of(name), 95); files[i].data = (uint8_t *)data; files[i].size = size; files[i].cap = 0; files[i].used = 1; files[i].ro = 1;
        return;
    }
}
void doom_files_reset(void)
{
    for (int i = 0; i < NFILES; i++) { if (files[i].used && !files[i].ro && files[i].data) free(files[i].data); files[i].used = 0; files[i].data = 0; }
    for (int i = 0; i < 8; i++) ftab[i].wr = ftab[i].console = 0, ftab[i].data = 0;
}
static mfile *find(const char *p) { const char *b = base_of(p); for (int i = 0; i < NFILES; i++) if (files[i].used && !strcasecmp(files[i].name, b)) return &files[i]; return 0; }

FILE *fopen(const char *p, const char *m)
{
    int w = m[0] == 'w' || m[0] == 'a' || m[1] == '+';
    mfile *mf = find(p);
    if (!w) { if (!mf) { errno = ENOENT; return 0; } }
    else {
        if (mf && mf->ro) return 0;
        if (!mf) {
            for (int i = 0; i < NFILES; i++) if (!files[i].used) { mf = &files[i]; break; }
            if (!mf) return 0;
            memset(mf, 0, sizeof *mf); strncpy(mf->name, base_of(p), 95); mf->used = 1;
        }
        if (m[0] == 'w') mf->size = 0;
    }
    for (int i = 0; i < 8; i++) if (!ftab[i].data && !ftab[i].console && ftab[i].wr == 0 && ftab[i].cap == 0) {
        ftab[i].data = (uint8_t *)mf; ftab[i].size = 0; ftab[i].cap = 1; ftab[i].pos = m[0] == 'a' ? mf->size : 0; ftab[i].wr = w; ftab[i].console = 0;
        return &ftab[i];
    }
    return 0;
}
int fclose(FILE *f) { if (f && !f->console) { f->data = 0; f->cap = 0; f->wr = 0; } return 0; }
static mfile *mf_of(FILE *f) { return (mfile *)f->data; }
size_t fread(void *b, size_t s, size_t n, FILE *f)
{
    if (!f || f->console || !s) return 0;
    mfile *m = mf_of(f);
    size_t want = s * n, have = f->pos < m->size ? m->size - f->pos : 0;
    if (want > have) want = have - have % s;
    memcpy(b, m->data + f->pos, want); f->pos += (uint32_t)want;
    return want / s;
}
size_t fwrite(const void *b, size_t s, size_t n, FILE *f)
{
    if (!f) return 0;
    if (f->console) { for (size_t i = 0; i < s * n; i++) doom_putc(((const char *)b)[i]); return n; }
    mfile *m = mf_of(f); size_t len = s * n;
    if (f->pos + len > m->cap) {
        uint32_t nc = (f->pos + (uint32_t)len + 4095u) & ~4095u;
        uint8_t *nd = realloc(m->data, nc); if (!nd) return 0;
        m->data = nd; m->cap = nc;
    }
    memcpy(m->data + f->pos, b, len); f->pos += (uint32_t)len; if (f->pos > m->size) m->size = f->pos;
    return n;
}
int fseek(FILE *f, long off, int wh)
{
    if (!f || f->console) return -1;
    mfile *m = mf_of(f); long np = wh == SEEK_SET ? off : wh == SEEK_CUR ? (long)f->pos + off : (long)m->size + off;
    if (np < 0) return -1;
    f->pos = (uint32_t)np; return 0;
}
long ftell(FILE *f) { return f && !f->console ? (long)f->pos : -1; }
int feof(FILE *f) { return f && !f->console && f->pos >= mf_of(f)->size; }
int fflush(FILE *f) { (void)f; return 0; }
int fgetc(FILE *f) { unsigned char c; return fread(&c, 1, 1, f) == 1 ? c : EOF; }
char *fgets(char *b, int n, FILE *f)
{
    int i = 0, c;
    while (i < n - 1 && (c = fgetc(f)) != EOF) { b[i++] = (char)c; if (c == '\n') break; }
    if (!i) return 0;
    b[i] = 0; return b;
}
int fputc(int c, FILE *f) { char ch = (char)c; return fwrite(&ch, 1, 1, f) == 1 ? c : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { doom_putc((char)c); return c; }
int puts(const char *s) { while (*s) doom_putc(*s++); doom_putc('\n'); return 0; }
int fputs(const char *s, FILE *f) { return (int)fwrite(s, 1, strlen(s), f); }
int vfprintf(FILE *f, const char *fmt, va_list ap) { char b[512]; int n = vsnprintf(b, sizeof b, fmt, ap); fwrite(b, 1, n < (int)sizeof b ? (size_t)n : sizeof b - 1, f); return n; }
int fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r; }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r; }
int remove(const char *p) { mfile *m = find(p); if (!m || m->ro) return -1; if (m->data) free(m->data); m->used = 0; m->data = 0; return 0; }
int rename(const char *a, const char *b)
{
    mfile *m = find(a); if (!m || m->ro) return -1;
    remove(b); strncpy(m->name, base_of(b), 95); return 0;
}

/* ---------- выход из игры: возврат в оболочку через setjmp-подобную точку ---------- */
uint32_t doom_jb[6];
__asm__(".text\n.globl doom_setjmp\ndoom_setjmp:\n"
        " movl %ebx, doom_jb\n movl %esi, doom_jb+4\n movl %edi, doom_jb+8\n movl %ebp, doom_jb+12\n"
        " leal 4(%esp), %eax\n movl %eax, doom_jb+16\n movl (%esp), %eax\n movl %eax, doom_jb+20\n"
        " xorl %eax, %eax\n ret\n"
        ".globl doom_longjmp\ndoom_longjmp:\n"
        " movl 4(%esp), %eax\n movl doom_jb, %ebx\n movl doom_jb+4, %esi\n movl doom_jb+8, %edi\n movl doom_jb+12, %ebp\n"
        " movl doom_jb+16, %esp\n jmp *doom_jb+20\n");
void doom_leave(int code) { doom_longjmp(code ? code : 1); }
void exit(int c) { doom_exit_hook(c); doom_leave(c ? c : 1); }
void abort(void) { exit(-1); }

/* Помощники, которых ждёт код Chocolate Doom (midifile.c, i_oplmusic.c, mus2mid.c) */
uint32_t SDL_SwapBE32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24); }
uint16_t SDL_SwapBE16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
FILE *M_fopen(const char *p, const char *m) { return fopen(p, m); }
int M_remove(const char *p) { return remove(p); }
extern void I_Error(const char *error, ...);
void *I_Realloc(void *p, size_t n)
{
    void *r = realloc(p, n);
    if (!r && n) I_Error("I_Realloc: failed on reallocation of %i bytes", (int)n);
    return r;
}
