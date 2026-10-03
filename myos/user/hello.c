#include "syscall.h"

static int my_strlen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int main(void) {
    const char* msg =
        "========================================\n"
        "  Hello from userspace ELF program!\n"
        "  I am running in ring3, loaded by kernel.\n"
        "========================================\n";

    sys_write(msg, my_strlen(msg));

    const char* p = "getpid() = ";
    sys_write(p, my_strlen(p));

    int pid = sys_getpid();
    /* печатаем цифру pid (0..9) */
    char c = '0' + (pid % 10);
    sys_write(&c, 1);
    sys_write("\n", 1);

    return 42;
}
