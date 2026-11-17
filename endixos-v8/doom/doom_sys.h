#ifndef DOOM_SYS_H
#define DOOM_SYS_H
#include <stdint.h>
/* Связь движка Doom с ядром EndixOS (реализует doom_plat.c; в тестах - заглушки). */
void doom_putc(char c);                       /* текст консоли движка (последовательный порт) */
void doom_exit_hook(int code);                /* вызывается из exit() до выхода из игры */
void doom_mem_init(void *base, uint32_t size);
uint32_t doom_mem_free(void);                 /* самый большой свободный кусок */
void doom_file_add(const char *name, const void *data, uint32_t size);
void doom_files_reset(void);
int  doom_setjmp(void) __attribute__((returns_twice));
void doom_longjmp(int v) __attribute__((noreturn));
void doom_leave(int code) __attribute__((noreturn));
#endif
