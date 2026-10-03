#ifndef PMM_H
#define PMM_H
#include <stdint.h>

#define PAGE_SIZE 4096

void     pmm_init(uint32_t mb_info_addr);
void*    pmm_alloc_page(void);   /* возвращает физический адрес или 0 */
void     pmm_free_page(void* p);
uint32_t pmm_total_pages(void);
uint32_t pmm_used_pages(void);
uint32_t pmm_free_pages(void);

#endif
