#include "heap.h"
#include "tty.h"
#include <stdint.h>

/*
 * Простейший heap с двусвязным списком блоков и coalescing при free.
 * Живёт по фиксированному адресу в identity-mapped области (4..12 MB).
 */

#define HEAP_START   0x00400000u    /* 4 MB */
#define HEAP_SIZE    0x00800000u    /* 8 MB */

typedef struct block {
    size_t        size;             /* payload size (без заголовка) */
    int           free;
    struct block* next;
    struct block* prev;
} block_t;

#define BLOCK_HDR  sizeof(block_t)
#define ALIGN4(x)  (((x) + 3u) & ~3u)

static block_t* head = NULL;
static size_t   total_bytes = 0;
static size_t   used_bytes  = 0;

void heap_init(void) {
    head = (block_t*)HEAP_START;
    head->size = HEAP_SIZE - BLOCK_HDR;
    head->free = 1;
    head->next = NULL;
    head->prev = NULL;

    total_bytes = HEAP_SIZE;
    used_bytes  = BLOCK_HDR;

    tty_puts("[ok] Heap at ");
    tty_put_hex(HEAP_START);
    tty_puts(" size ");
    tty_put_hex(HEAP_SIZE);
    tty_putc('\n');
}

/* Разбить блок, если хватает на новый блок + полезная нагрузка */
static void split(block_t* b, size_t size) {
    if (b->size < size + BLOCK_HDR + 16) return;

    block_t* nb = (block_t*)((uint8_t*)b + BLOCK_HDR + size);
    nb->size = b->size - size - BLOCK_HDR;
    nb->free = 1;
    nb->next = b->next;
    nb->prev = b;
    if (nb->next) nb->next->prev = nb;
    b->next = nb;
    b->size = size;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN4(size);

    for (block_t* b = head; b; b = b->next) {
        if (b->free && b->size >= size) {
            split(b, size);
            b->free = 0;
            used_bytes += b->size + BLOCK_HDR;
            return (uint8_t*)b + BLOCK_HDR;
        }
    }
    return NULL;  /* out of heap */
}

void* kzalloc(size_t size) {
    void* p = kmalloc(size);
    if (!p) return NULL;
    uint8_t* b = (uint8_t*)p;
    for (size_t i = 0; i < size; i++) b[i] = 0;
    return p;
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_t* b = (block_t*)((uint8_t*)ptr - BLOCK_HDR);
    if (b->free) return;  /* double free */

    b->free = 1;
    used_bytes -= b->size + BLOCK_HDR;

    /* coalesce с соседями */
    block_t* cur = head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += BLOCK_HDR + cur->next->size;
            cur->next = cur->next->next;
            if (cur->next) cur->next->prev = cur;
        } else {
            cur = cur->next;
        }
    }
}

size_t heap_total(void) { return total_bytes; }
size_t heap_used(void)  { return used_bytes; }
size_t heap_free(void)  { return total_bytes - used_bytes; }
