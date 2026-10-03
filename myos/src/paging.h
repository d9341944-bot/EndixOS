#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

void     paging_init(void);
int      paging_enable(void);
uint32_t paging_pd_phys(void);
void     paging_map_4mb(uint32_t virt, uint32_t phys, int user, int rw);
void     paging_unmap(uint32_t virt);

#endif
