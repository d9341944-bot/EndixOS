#ifndef THREAD_H
#define THREAD_H
#include <stdint.h>

typedef void (*thread_fn)(void);

#define THREAD_STACK_SIZE 8192
#define MAX_THREADS       16

#define THREAD_SLEEPING   4
#define THREAD_ZOMBIE     5

typedef struct thread {
    uint32_t esp;
    uint32_t stack_base;
    int      id;
    int      state;          /* 0=free 1=ready 2=running 3=dead */
    char     name[16];
    uint32_t ticks;
    uint32_t sleep_ticks;
    uint32_t kernel_stack_top;
    int      exit_code;
    int      parent;        /* id родителя или -1 */
    int      waiting_for;   /* id ребёнка, которого ждём, или -1 */
} thread_t;

void      thread_init(void);
int       thread_create(thread_fn fn, const char* name);
void      schedule(void);
void      thread_yield(void);
void      thread_sleep(uint32_t ticks);
void      thread_tick(void);
void      thread_exit(void);
int       thread_wait(int child_id);   /* → exit_code или -1 */
int       thread_get_exit_code(int id);
int       thread_current_id(void);
int       thread_count(void);
thread_t* thread_get(int id);

#endif
