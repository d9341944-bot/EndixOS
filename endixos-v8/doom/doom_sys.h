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
/* звук: микшер в i_endixsound.c, вывод - ядро (HDA, циклический буфер стерео 48 кГц 16 бит) */
void doom_snd_pump(void);                     /* раз в кадр: домешать звук вперёд от позиции воспроизведения */
void doom_snd_stop(void);
void doom_snd_resume(void);                   /* окно снова активно: запустить вывод заново */
int  doom_audio_start(const int16_t *ring, uint32_t frames);   /* 1 - цикл запущен */
uint32_t doom_audio_pos(void);                /* позиция воспроизведения в кадрах от начала кольца */
void doom_audio_stop(void);
#endif
