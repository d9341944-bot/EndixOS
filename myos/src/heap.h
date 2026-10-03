#ifndef HEAP_H
#define HEAP_H
#include <stddef.h>

void  heap_init(void);
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void  kfree(void* ptr);

/* для команды 'heap' — статистика */
size_t heap_total(void);
size_t heap_used(void);
size_t heap_free(void);

#endif
