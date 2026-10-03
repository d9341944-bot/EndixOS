#include "../syscall.h"

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

int main(void) {
    const char* msg =
        "\n"
        "  ### Hello from an ELF file on disk! ###\n"
        "  This program was read via FAT16 and executed in ring3.\n"
        "\n";
    sys_write(msg, slen(msg));
    return 0;
}
