#include "syscall.h"

extern int main(void);

__attribute__((noreturn, section(".text.start")))
void _start(void) {
    sys_exit(main());
    /* sys_exit — noreturn, но GCC всё равно хочет __builtin_unreachable */
    __builtin_unreachable();
}
