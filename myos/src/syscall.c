#include "syscall.h"
#include "tty.h"
#include "thread.h"
#include "keyboard.h"
#include "fd.h"
#include "fat16.h"
#include "heap.h"
#include "elf.h"

extern volatile int g_in_syscall;
extern void thread_set_exit_code(int);

/* ABI:
 *   1 exit(code)
 *   2 write(ptr, len)
 *   3 getpid()
 *   4 readchar() -> char
 *   5 open(path) -> fd
 *   6 read(fd, buf, n) -> bytes
 *   7 close(fd)
 *   8 readdir(index, struct ude*) -> 0/-1
 */

extern int spawn_user_elf(const char* path);

void syscall_handler(struct regs* r) {
    switch (r->eax) {
        case 1:
            tty_set_color(0x0E);
            tty_puts("[sys] exit(");
            tty_put_hex(r->ebx);
            tty_puts(")\n");
            tty_set_color(0x0A);
            thread_set_exit_code((int)r->ebx);
            thread_exit();
            break;

        case 2: {
            const char* p = (const char*)r->ebx;
            uint32_t len  = r->ecx;
            tty_set_color(0x0D);
            for (uint32_t i = 0; i < len; i++) tty_putc(p[i]);
            tty_set_color(0x0A);
            tty_flush();
            break;
        }

        case 3:
            r->eax = (uint32_t)thread_current_id();
            break;

        case 4: {
            g_in_syscall = 1;
            __asm__ volatile ("sti");
            while (!keyboard_has_char()) __asm__ volatile ("hlt");
            __asm__ volatile ("cli");
            g_in_syscall = 0;
            r->eax = (uint32_t)keyboard_getchar();
            break;
        }

        case 5: {
            const char* path = (const char*)r->ebx;
            r->eax = (uint32_t)fd_open(path);
            break;
        }

        case 6: {
            int fd        = (int)r->ebx;
            void* buf     = (void*)r->ecx;
            uint32_t n    = r->edx;
            r->eax = fd_read(fd, buf, n);
            break;
        }

        case 7:
            r->eax = (uint32_t)fd_close((int)r->ebx);
            break;

        case 9: {
            const char* path = (const char*)r->ebx;
            int saved = g_in_syscall;
            g_in_syscall = 0;       /* разрешаем schedule во время yield */
            int id = spawn_user_elf(path);
            r->eax = (uint32_t)id;
            if (id > 0) thread_yield();
            g_in_syscall = saved;
            break;
        }

        case 8: {
            uint32_t idx  = r->ebx;
            struct ude* u = (struct ude*)r->ecx;
            r->eax = (uint32_t)fd_readdir(idx, u);
            break;
        }

        case 10: {  /* create(name) */
            const char* path = (const char*)r->ebx;
            r->eax = (uint32_t)fat16_create(path);
            break;
        }
        case 11: {  /* write_file(name, buf, n) */
            const char* path = (const char*)r->ebx;
            const uint8_t* buf = (const uint8_t*)r->ecx;
            uint32_t n = r->edx;
            r->eax = fat16_write(path, buf, n, 0);
            break;
        }
        case 12: {  /* unlink(name) */
            const char* path = (const char*)r->ebx;
            r->eax = (uint32_t)fat16_delete(path);
            break;
        }

        case 13: {   /* waitpid(child_id) -> exit_code */
            int saved = g_in_syscall;
            g_in_syscall = 0;
            r->eax = (uint32_t)thread_wait((int)r->ebx);
            g_in_syscall = saved;
            break;
        }

        case 14: {   /* erase cell at current tty cursor */
            extern void tty_erase_cell(void);
            extern void tty_flush(void);
            tty_erase_cell();
            tty_flush();
            break;
        }

        default:
            tty_set_color(0x0C);
            tty_puts("[sys] unknown ");
            tty_put_hex(r->eax);
            tty_putc('\n');
            tty_set_color(0x0A);
    }
}
