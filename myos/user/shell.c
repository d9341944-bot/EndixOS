#include "syscall.h"

#define LINE_MAX 128
#define HIST_MAX 16

#define KEY_UP     0x101
#define KEY_DOWN   0x102
#define KEY_LEFT   0x103
#define KEY_RIGHT  0x104
#define KEY_DELETE 0x105
#define KEY_HOME   0x106
#define KEY_END    0x107

static int  slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void puts_(const char* s) { sys_write(s, slen(s)); }
static int  seq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char line[LINE_MAX];
static int  line_len = 0;
static int  cursor   = 0;

static char hist[HIST_MAX][LINE_MAX];
static int  hist_count = 0;
static int  hist_pos   = -1;

static const char* cmd_list[] = {
    "help", "echo", "pid", "ls", "cat", "run",
    "touch", "rm", "exit", 0
};

static void prompt(void) { puts_("endix> "); }

static void redraw(int old_len) {
    int drawn = (line_len > old_len) ? line_len : old_len;
    static char buf[LINE_MAX * 2 + 32];
    int n = 0;
    buf[n++] = '\r';
    const char* p = "endix> ";
    while (*p) buf[n++] = *p++;
    for (int i = 0; i < line_len; i++) buf[n++] = line[i];
    for (int i = line_len; i < drawn; i++) buf[n++] = ' ';
    for (int i = 0; i < drawn - cursor; i++) buf[n++] = '\b';
    sys_write(buf, n);
}

static void push_history(const char* s) {
    if (!s[0]) return;
    if (hist_count > 0 && seq((char*)hist[(hist_count-1) % HIST_MAX], s)) return;
    int idx = hist_count % HIST_MAX;
    int i = 0;
    while (s[i] && i < LINE_MAX - 1) { hist[idx][i] = s[i]; i++; }
    hist[idx][i] = 0;
    hist_count++;
}

static void load_history(int delta) {
    if (hist_count == 0) return;
    int old = line_len;
    int p = hist_pos < 0 ? hist_count : hist_pos;
    p += delta;
    if (p < 0) p = 0;
    if (p > hist_count) p = hist_count;
    hist_pos = p;
    if (p == hist_count) { line_len = 0; line[0] = 0; }
    else {
        const char* h = hist[p % HIST_MAX];
        int i = 0;
        while (h[i] && i < LINE_MAX - 1) { line[i] = h[i]; i++; }
        line_len = i;
    }
    cursor = line_len;
    redraw(old);
}

static void do_complete(void) {
    if (line_len == 0) return;

    char* last = line;
    for (int i = line_len - 1; i >= 0; i--)
        if (line[i] == ' ') { last = line + i + 1; break; }

    int len = line_len - (int)(last - line);
    const char* match = 0;
    int match_count = 0;

    for (int i = 0; cmd_list[i]; i++) {
        int ok = 1;
        for (int j = 0; j < len; j++)
            if (cmd_list[i][j] != last[j]) { ok = 0; break; }
        if (!ok) continue;
        match = cmd_list[i];
        match_count++;
    }
    if (match_count == 0) return;

    int old = line_len;
    for (int k = len; match[k]; k++)
        if (line_len < LINE_MAX - 1) { line[line_len++] = match[k]; cursor = line_len; }
    if (line_len < LINE_MAX - 1) { line[line_len++] = ' '; cursor = line_len; }
    line[line_len] = 0;
    redraw(old);

    if (match_count > 1) {
        puts_("\n");
        int printed = 0;
        for (int i = 0; cmd_list[i]; i++) {
            int ok = 1;
            for (int j = 0; j < len; j++)
                if (cmd_list[i][j] != last[j]) { ok = 0; break; }
            if (!ok) continue;
            puts_(cmd_list[i]); puts_("  ");
            if (++printed % 5 == 0) puts_("\n");
        }
        if (printed % 5) puts_("\n");
        puts_("\n");
        prompt();
        sys_write(line, line_len);
    }
}

/* ---------- команды ---------- */

static void cmd_help(void) {
    puts_("commands:\n"
          "  help         - this help\n"
          "  echo <t>     - print text\n"
          "  echo t > f   - write text to file\n"
          "  pid          - show pid\n"
          "  ls           - list FAT16 root\n"
          "  cat <file>   - print file\n"
          "  run <file>   - execute ELF from disk\n"
          "  touch <f>    - create empty file\n"
          "  rm <f>       - delete file\n"
          "  exit         - return to kernel\n");
}

static void cmd_pid(void) {
    int p = sys_getpid();
    char buf[16]; int n = 0;
    if (p == 0) buf[n++] = '0';
    else {
        char t[12]; int m = 0;
        while (p > 0) { t[m++] = '0' + (p % 10); p /= 10; }
        while (m > 0) buf[n++] = t[--m];
    }
    buf[n++] = '\n';
    sys_write(buf, n);
}

static void cmd_ls(void) {
    puts_("-- FAT16 root --\n");
    struct ude u;
    for (int i = 0; sys_readdir(i, &u) == 0; i++) {
        puts_(u.name);
        if (u.is_dir) puts_("/");
        puts_("    ");
        char h[11];
        h[0]='0'; h[1]='x';
        for (int k = 0; k < 8; k++) {
            unsigned int nib = (u.size >> ((7-k)*4)) & 0xF;
            h[2+k] = nib < 10 ? ('0'+nib) : ('a'+nib-10);
        }
        h[10] = '\n';
        sys_write(h, 11);
    }
    puts_("-- end --\n");
}

static void cmd_cat(const char* name) {
    int fd = sys_open(name);
    if (fd < 0) { puts_("not found: "); puts_(name); puts_("\n"); return; }
    puts_("--- "); puts_(name); puts_(" ---\n");
    char buf[256]; int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) sys_write(buf, n);
    sys_close(fd);
    puts_("\n--- end ---\n");
}

static void cmd_touch(const char* name) {
    if (sys_create(name) == 0) puts_("created\n");
    else                       puts_("create failed\n");
}

static void cmd_rm(const char* name) {
    if (sys_unlink(name) == 0) puts_("removed\n");
    else                       puts_("remove failed\n");
}

static char* find_redir(char* s) {
    while (*s) { if (*s == '>') return s; s++; }
    return 0;
}

static void cmd_echo(char* args) {
    char* p = find_redir(args);
    if (!p) { puts_(args); puts_("\n"); return; }
    char* end = p - 1;
    while (end >= args && *end == ' ') end--;
    end[1] = 0;
    char* name = p + 1;
    while (*name == ' ') name++;
    sys_create(name);
    int n = slen(args);
    if (sys_write_file(name, args, n) < 0) puts_("write failed\n");
    else                                   puts_("ok\n");
}

static void exec_cmd(char* cmd) {
    if      (seq(cmd, "help"))  cmd_help();
    else if (seq(cmd, "pid"))   cmd_pid();
    else if (seq(cmd, "ls"))    cmd_ls();
    else if (seq(cmd, "exit"))  sys_exit(0);
    else if (cmd[0]=='e' && cmd[1]=='c' && cmd[2]=='h' && cmd[3]=='o' && cmd[4]==' ')
        cmd_echo(cmd + 5);
    else if (cmd[0]=='c' && cmd[1]=='a' && cmd[2]=='t' && cmd[3]==' ')
        cmd_cat(cmd + 4);
    else if (cmd[0]=='t' && cmd[1]=='o' && cmd[2]=='u' && cmd[3]=='c' && cmd[4]=='h' && cmd[5]==' ')
        cmd_touch(cmd + 6);
    else if (cmd[0]=='r' && cmd[1]=='m' && cmd[2]==' ')
        cmd_rm(cmd + 3);
    else if (cmd[0]=='r' && cmd[1]=='u' && cmd[2]=='n' && cmd[3]==' ') {
        int id = sys_spawn(cmd + 4);
        if (id < 0) { puts_("spawn failed\n"); return; }
        int code = sys_waitpid(id);
        puts_("[exit "); 
        char b[8]; int n = 0;
        if (code < 0) { b[n++]='-'; code = -code; }
        if (code == 0) b[n++] = '0';
        else { char t[8]; int m = 0; while (code>0) { t[m++]='0'+(code%10); code/=10; } while (m>0) b[n++]=t[--m]; }
        b[n++]=']'; b[n++]='\n';
        sys_write(b, n);
    }
    else if (cmd[0] == 0) { }
    else { puts_("unknown: "); puts_(cmd); puts_("\n"); }
}

int main(void) {
    puts_("=== EndixOS userspace shell ===\n"
          "Tab: complete, Up/Down: history, Left/Right: edit\n\n");
    prompt();
    for (;;) {
        int c = sys_readchar();

        if (c == '\n') {
            puts_("\n");
            line[line_len] = 0;
            push_history(line);
            hist_pos = -1;
            exec_cmd(line);
            line_len = 0; cursor = 0;
            prompt();
        }
        else if (c == '\b') {
            if (cursor > 0) {
                if (cursor == line_len) {
                    line_len--; cursor--;
                    line[line_len] = 0;
                    puts_("\b");
                    sys_erase_cell();
                } else {
                    int old = line_len;
                    for (int i = cursor - 1; i < line_len - 1; i++) line[i] = line[i+1];
                    line_len--; cursor--;
                    line[line_len] = 0;
                    redraw(old);
                }
            }
        }
        else if (c == KEY_DELETE) {
            if (cursor < line_len) {
                int old = line_len;
                for (int i = cursor; i < line_len - 1; i++) line[i] = line[i+1];
                line_len--;
                line[line_len] = 0;
                redraw(old);
            }
        }
        else if (c == KEY_LEFT)  { if (cursor > 0) { cursor--; puts_("\b"); } }
        else if (c == KEY_RIGHT) { if (cursor < line_len) { sys_write(&line[cursor], 1); cursor++; } }
        else if (c == KEY_HOME)  { while (cursor > 0) { cursor--; puts_("\b"); } }
        else if (c == KEY_END)   { while (cursor < line_len) { sys_write(&line[cursor], 1); cursor++; } }
        else if (c == KEY_UP)    { load_history(-1); }
        else if (c == KEY_DOWN)  { load_history(+1); }
        else if (c == '\t')      { do_complete(); }
        else if (c >= 32 && c < 127) {
            if (line_len < LINE_MAX - 1) {
                if (cursor == line_len) {
                    line[line_len++] = (char)c;
                    cursor = line_len;
                    line[line_len] = 0;
                    sys_write((char*)&c, 1);
                } else {
                    int old = line_len;
                    for (int i = line_len; i > cursor; i--) line[i] = line[i-1];
                    line[cursor] = (char)c;
                    line_len++; cursor++;
                    line[line_len] = 0;
                    redraw(old);
                }
            }
        }
    }
    return 0;
}
