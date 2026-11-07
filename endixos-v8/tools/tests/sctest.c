#include "gui/photos.c"
#include "gui/scenery.c"
extern int printf(const char *, ...);
__asm__(".section .rodata\n.global _binary_wall_pak_start\n.global _binary_wall_pak_end\n_binary_wall_pak_start:\n.incbin \"assets/wall.pak\"\n_binary_wall_pak_end:\n.text\n");
static u32 big[1280 * 800], th[160 * 100];
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 avg(u32 *p, int n) { u64 s = 0; for (int i = 0; i < n; i++) s += ((p[i] >> 16 & 255) + (p[i] >> 8 & 255) + (p[i] & 255)) / 3; return (u32)(s / n); }
int main(void)
{
    CHECK(scenery_count() == 5, "в паке 5 сцен");
    for (int k = 0; k < scenery_count(); k++) {
        CHECK(scenery_name(k)[0] != 0, scenery_name(k));
        CHECK(scenery_paint(k, big, 1280, 800) && avg(big, 1280 * 800) > 15, "полный кадр декодируется");
        CHECK(scenery_paint(k, th, 160, 100) && avg(th, 160 * 100) > 15, "миниатюра декодируется");
    }
    CHECK(!scenery_paint(9, th, 160, 100), "несуществующая сцена отклонена");
    return fails;
}
