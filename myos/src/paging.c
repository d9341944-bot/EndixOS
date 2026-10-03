#include "paging.h"
#include "tty.h"

#define PDE_PRESENT  0x01
#define PDE_RW       0x02
#define PDE_USER     0x04
#define PDE_PS       0x80

#define PDE_INDEX(v) ((v) >> 22)

static uint32_t page_directory[1024] __attribute__((aligned(4096)));

static int cpu_has_pse(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx >> 3) & 1;
}

void paging_init(void) {
    /* Identity-map все 4 ГБ через 4-МБ страницы.
       Это учебное решение: просто, предсказуемо, покрывает
       и kernel, и heap, и framebuffer, и MMIO. */
    for (uint32_t i = 0; i < 1024; i++) {
        page_directory[i] = (i * 0x400000u) | PDE_PRESENT | PDE_RW | PDE_PS | PDE_USER;
    }
    tty_puts("[ok] Paging: identity-mapped all 4 GB (4 MB pages)\n");
}

int paging_enable(void) {
    if (!cpu_has_pse()) {
        tty_puts("[paging] CPU lacks PSE\n");
        return -1;
    }

    uint32_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 4);
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile ("mov %0, %%cr3" :: "r"((uint32_t)page_directory));

    __asm__ volatile (
        "mov %%cr0, %%eax\n"
        "or  $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        "jmp 1f\n"
        "1:\n"
        ::: "eax"
    );
    return 0;
}

uint32_t paging_pd_phys(void) { return (uint32_t)page_directory; }

void paging_map_4mb(uint32_t virt, uint32_t phys, int user, int rw) {
    uint32_t idx = PDE_INDEX(virt);
    uint32_t e = (phys & 0xFFC00000u) | PDE_PRESENT | PDE_PS;
    if (rw)   e |= PDE_RW;
    if (user) e |= PDE_USER;
    page_directory[idx] = e;
    __asm__ volatile ("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
}

void paging_unmap(uint32_t virt) {
    page_directory[PDE_INDEX(virt)] = 0;
    __asm__ volatile ("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
}
