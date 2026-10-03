#include "pmm.h"
#include "multiboot.h"
#include "tty.h"

#define MAX_PAGES    (1024u * 1024u)
#define BITMAP_SIZE  (MAX_PAGES / 8)

static uint8_t  bitmap[BITMAP_SIZE];
static uint32_t total_pages = 0;
static uint32_t used_pages  = 0;
static uint32_t last_free   = 0;

static inline void bit_set(uint32_t i)   { bitmap[i >> 3] |=  (1u << (i & 7)); }
static inline void bit_clear(uint32_t i) { bitmap[i >> 3] &= ~(1u << (i & 7)); }
static inline int  bit_get(uint32_t i)   { return (bitmap[i >> 3] >> (i & 7)) & 1; }

extern uint8_t kernel_start[];
extern uint8_t kernel_end[];

static void mark_used(uint32_t page) {
    if (page >= total_pages) return;
    if (!bit_get(page)) { bit_set(page); used_pages++; }
}

static void mark_free(uint32_t page) {
    if (page >= total_pages) return;
    if (bit_get(page)) { bit_clear(page); used_pages--; }
}

void pmm_init(uint32_t mb_info_addr) {
    for (uint32_t i = 0; i < BITMAP_SIZE; i++) bitmap[i] = 0xFF;
    total_pages = MAX_PAGES;
    used_pages  = MAX_PAGES;

    struct multiboot_info* mbi = (struct multiboot_info*)mb_info_addr;
    if (!(mbi->flags & (1 << 6))) {
        tty_puts("[pmm] no memory map!\n");
        return;
    }

    uint32_t mmap_end   = mbi->mmap_addr + mbi->mmap_length;
    uint32_t max_page   = 0;
    uint32_t free_count = 0;

    for (uint32_t p = mbi->mmap_addr; p < mmap_end; ) {
        struct multiboot_mmap_entry* e = (struct multiboot_mmap_entry*)p;
        if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t start = e->addr;
            uint64_t end   = e->addr + e->len;
            start = (start + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
            end   = end & ~((uint64_t)PAGE_SIZE - 1);

            for (uint64_t a = start; a + PAGE_SIZE <= end; a += PAGE_SIZE) {
                uint32_t page = (uint32_t)(a >> 12);
                if (page < MAX_PAGES) {
                    bit_clear(page);
                    free_count++;
                    if (page > max_page) max_page = page;
                }
            }
        }
        p += e->size + 4;
    }

    /* total_pages — по фактической памяти, а не по 4 ГБ */
    total_pages = max_page + 1;
    used_pages  = total_pages - free_count;

    /* Помечаем занятым всё до конца ядра и страницу 0 */
    uint32_t k_end = (((uint32_t)kernel_end) + PAGE_SIZE - 1) >> 12;
    for (uint32_t pg = 0; pg < k_end; pg++) mark_used(pg);
    mark_used(0);

    tty_puts("[ok] PMM: total ");
    tty_put_hex(total_pages);
    tty_puts(" pages, used ");
    tty_put_hex(used_pages);
    tty_puts(", free ");
    tty_put_hex(total_pages - used_pages);
    tty_putc('\n');
}

void* pmm_alloc_page(void) {
    for (uint32_t i = last_free; i < total_pages; i++) {
        if (!bit_get(i)) { bit_set(i); used_pages++; last_free = i + 1; return (void*)(i << 12); }
    }
    for (uint32_t i = 0; i < last_free && i < total_pages; i++) {
        if (!bit_get(i)) { bit_set(i); used_pages++; last_free = i + 1; return (void*)(i << 12); }
    }
    return 0;
}

void pmm_free_page(void* p) {
    uint32_t page = ((uint32_t)p) >> 12;
    if (page >= total_pages) return;
    if (!bit_get(page)) return;
    bit_clear(page);
    used_pages--;
    if (page < last_free) last_free = page;
}

uint32_t pmm_total_pages(void) { return total_pages; }
uint32_t pmm_used_pages(void)  { return used_pages; }
uint32_t pmm_free_pages(void)  { return total_pages - used_pages; }
