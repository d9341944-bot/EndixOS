#include "thread.h"
#include "heap.h"
#include "tty.h"
#include "gdt.h"

volatile int g_in_syscall = 0;
#include <stdint.h>

#define STATE_FREE     0
#define STATE_READY    1
#define STATE_RUNNING  2
#define STATE_DEAD     3
#define STATE_SLEEPING 4
#define STATE_ZOMBIE   5

static thread_t threads[MAX_THREADS];
static int      n_threads = 0;
static int      current   = 0;

extern void switch_to(uint32_t* old_esp, uint32_t new_esp);
extern void thread_start(void);

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void thread_init(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].state       = STATE_FREE;
        threads[i].id          = -1;
        threads[i].sleep_ticks = 0;
    }
    threads[0].id           = 0;
    threads[0].state        = STATE_RUNNING;
    threads[0].esp          = 0;
    threads[0].ticks        = 0;
    threads[0].sleep_ticks  = 0;
    threads[0].stack_base   = 0;
    threads[0].exit_code    = 0;
    threads[0].parent       = -1;
    threads[0].waiting_for  = -1;
    threads[0].kernel_stack_top = 0;    /* устанавливается в kernel_main */
    str_copy(threads[0].name, "kernel", 16);
    n_threads = 1;
    current   = 0;
    tty_puts("[ok] Threads: kernel thread id=0\n");
}

int thread_create(thread_fn fn, const char* name) {
    if (n_threads >= MAX_THREADS) return -1;
    int id = n_threads++;
    thread_t* t = &threads[id];

    t->id           = id;
    t->state        = STATE_READY;
    t->ticks        = 0;
    t->sleep_ticks  = 0;
    t->exit_code    = 0;
    t->parent       = current;
    t->waiting_for  = -1;
    str_copy(t->name, name, 16);

    t->stack_base = (uint32_t)kmalloc(THREAD_STACK_SIZE);
    if (!t->stack_base) { n_threads--; return -1; }

    uint32_t kstack = (uint32_t)kmalloc(4096);
    if (!kstack) { kfree((void*)t->stack_base); n_threads--; return -1; }
    t->kernel_stack_top = kstack + 4096;

    uint32_t top = (t->stack_base + THREAD_STACK_SIZE) & ~0xFu;
    uint32_t* sp = (uint32_t*)top;
    *--sp = (uint32_t)thread_start;
    *--sp = 0;
    *--sp = (uint32_t)fn;
    *--sp = 0;
    *--sp = 0;
    t->esp = (uint32_t)sp;

    tty_puts("[thread] created '");
    tty_puts(name);
    tty_puts("' id=");
    tty_put_hex(id);
    tty_putc('\n');
    return id;
}

void schedule(void) {
    if (g_in_syscall) return;
    if (n_threads <= 1) return;

    int prev = current;
    int next = -1;

    for (int i = 1; i <= n_threads; i++) {
        int idx = (prev + i) % n_threads;
        if (threads[idx].state == STATE_READY ||
            threads[idx].state == STATE_RUNNING) {
            next = idx;
            break;
        }
    }
    if (next < 0 || next == prev) return;

    if (threads[prev].state == STATE_RUNNING)
        threads[prev].state = STATE_READY;
    threads[next].state = STATE_RUNNING;
    current = next;

    gdt_set_kernel_stack(threads[next].kernel_stack_top);
    switch_to(&threads[prev].esp, threads[next].esp);
}

void thread_yield(void) {
    __asm__ volatile ("cli");
    schedule();
    __asm__ volatile ("sti");
}

void thread_tick(void) {
    for (int i = 0; i < n_threads; i++) {
        if (threads[i].state == STATE_SLEEPING && threads[i].sleep_ticks > 0) {
            threads[i].sleep_ticks--;
            if (threads[i].sleep_ticks == 0)
                threads[i].state = STATE_READY;
        }
    }
}

void thread_sleep(uint32_t ticks) {
    if (ticks == 0) { thread_yield(); return; }
    __asm__ volatile ("cli");
    threads[current].sleep_ticks = ticks;
    threads[current].state       = STATE_SLEEPING;
    schedule();
    __asm__ volatile ("sti");
}

void thread_exit(void) {
    __asm__ volatile ("cli");
    threads[current].state = STATE_ZOMBIE;
    /* Разбудим родителя, если он ждёт нас */
    for (int i = 0; i < n_threads; i++) {
        if (threads[i].waiting_for == current && threads[i].state == STATE_SLEEPING) {
            threads[i].state = STATE_READY;
            threads[i].waiting_for = -1;
        }
    }
    schedule();
    for (;;) __asm__ volatile ("hlt");
}

void thread_set_exit_code(int code) {
    threads[current].exit_code = code;
}

int thread_get_exit_code(int id) {
    if (id < 0 || id >= n_threads) return -1;
    return threads[id].exit_code;
}

/* Родитель ждёт завершения ребёнка.
   Если ребёнок уже зомби или мёртв — возвращаем exit_code сразу.
   Иначе блокируемся до его завершения. */
int thread_wait(int child_id) {
    if (child_id < 0 || child_id >= n_threads) return -1;
    thread_t* child = &threads[child_id];

    /* Уже завершился? */
    if (child->state == STATE_ZOMBIE || child->state == STATE_DEAD) {
        int code = child->exit_code;
        child->state = STATE_FREE;
        return code;
    }

    /* Блокируемся */
    __asm__ volatile ("cli");
    threads[current].waiting_for = child_id;
    threads[current].state       = STATE_SLEEPING;
    threads[current].sleep_ticks = 0;   /* не time-based, а event-based */
    schedule();
    __asm__ volatile ("sti");

    /* Проснулись — ребёнок завершился */
    int code = child->exit_code;
    child->state = STATE_FREE;
    return code;
}

int       thread_current_id(void) { return current; }
int       thread_count(void)      { return n_threads; }
thread_t* thread_get(int id) {
    if (id < 0 || id >= n_threads) return 0;
    return &threads[id];
}
