/* Хост-тест драйвера HD Audio: подставной контроллер и кодек (динамик, наушники, неподключённый вывод, смеситель, ЦАП). */
extern int printf(const char *, ...); extern int puts(const char *);
extern void *memset(void *, int, unsigned long);
#define HDA_HOST 1
#include "kernel/types.h"
#include "drivers/hda.c"
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)

static u8 R[0x400];
static u32 corbrp_, rirbwp_;
static struct { u32 stream[8], fmt[8], amp[8], pinctl[8], eapd[8], power[8], sel[8], inamp[8]; } cs;

static u32 respond(u32 nid, u32 v)
{
    u32 verb12 = (v >> 8) & 0xFFF, pl = v & 0xFF, verb4 = (v >> 16) & 0xF;
    if (verb4 == 0x2) { cs.fmt[nid] = v & 0xFFFF; return 0; }
    if (verb4 == 0x3) { if (v & 0x8000) cs.amp[nid] = v & 0xFFFF; else cs.inamp[nid] = v & 0xFFFF; return 0; }
    switch (verb12) {
    case 0xF00:
        switch (pl) {
        case 0x04: return nid == 0 ? (1u << 16 | 1) : nid == 1 ? (2u << 16 | 5) : 0;
        case 0x05: return 1;
        case 0x09: return nid == 2 ? 0x00000004u | 0x100 * 0 : nid == 3 ? (2u << 20 | 0x106) : (4u << 20 | 0x104);
        case 0x0C: return nid == 4 ? 0x10010 : 0x10;
        case 0x0E: return nid == 2 ? 0 : 1;
        case 0x12: return 0x80000000u | (0x40u << 8) | 0x27;
        case 0x0D: return 0x80000000u | (0x20u << 8) | 0x10;
        }
        return 0;
    case 0xF02: return nid == 3 ? 2 : (nid >= 4 ? 3 : 0);
    case 0xF1C: return nid == 4 ? 0x90170110u : nid == 5 ? 0x0221101Fu : 0x40000000u | (1u << 20);
    case 0x706: cs.stream[nid] = pl; return 0;
    case 0x707: cs.pinctl[nid] = pl; return 0;
    case 0x70C: cs.eapd[nid] = pl; return 0;
    case 0x705: cs.power[nid] = pl; return 0;
    case 0x701: cs.sel[nid] = pl; return 0;
    }
    return 0;
}

static void process(void)
{
    u32 *corbm = (u32 *)(unsigned long)*(u32 *)(R + CORBLBASE);
    u32 *rirbm = (u32 *)(unsigned long)*(u32 *)(R + RIRBLBASE);
    u32 wp = *(u16 *)(R + CORBWP) & 0xFF;
    while (corbrp_ != wp) {
        corbrp_ = (corbrp_ + 1) & 0xFF;
        u32 e = corbm[corbrp_];
        u32 resp = respond((e >> 20) & 0xFF, e & 0xFFFFF);
        rirbwp_ = (rirbwp_ + 1) & 0xFF;
        rirbm[rirbwp_ * 2] = resp; rirbm[rirbwp_ * 2 + 1] = 0;
    }
    *(u16 *)(R + CORBRP) = (u16)corbrp_; *(u16 *)(R + RIRBWP) = (u16)rirbwp_;
}

u32 sim_rd(u32 off, int size)
{
    u32 v = 0;
    for (int i = 0; i < size; i++) v |= (u32)R[off + i] << (8 * i);
    return v;
}
void sim_wr(u32 off, int size, u32 v)
{
    if (off == GCTL && (v & 1) && !(R[GCTL] & 1)) { *(u16 *)(R + STATESTS) = 1; }
    if (off == CORBRP && size == 2 && (v & 0x8000)) { corbrp_ = 0; *(u16 *)(R + CORBRP) = 0x8000; return; }
    if (off == RIRBWP && size == 2 && (v & 0x8000)) { rirbwp_ = 0; *(u16 *)(R + RIRBWP) = 0; return; }
    for (int i = 0; i < size; i++) R[off + i] = (u8)(v >> (8 * i));
    if (off == CORBWP) process();
}

int main(void)
{
    memset(R, 0, sizeof R);
    *(u16 *)(R + GCAP) = (4 << 12) | (4 << 8) | 1;                     /* 4 выходных, 4 входных потока */
    R[CORBSIZE] = 0x40; R[RIRBSIZE] = 0x40;                           /* поддерживают 256 записей */
    CHECK(hda_attach() == 1, "контроллер и кодек настроены");
    CHECK(hda_present(), "hda_present");
    CHECK(cs.stream[2] == 0x10 && cs.fmt[2] == 0x0011, "ЦАП: поток 1, 48 кГц 16 бит стерео");
    CHECK(cs.pinctl[4] == 0x40 && cs.pinctl[5] == 0xC0, "динамик и наушники включены (HP_EN у наушников)");
    CHECK(cs.pinctl[6] == 0, "неподключённый вывод не трогаем");
    CHECK(cs.eapd[4] == 2, "EAPD включён у динамика");
    CHECK(cs.sel[4] == 0 && cs.sel[3] == 0, "выбраны нужные входы по пути");
    CHECK(cs.power[2] == 0 && cs.power[3] == 0 && cs.power[4] == 0, "питание D0 по пути");
    CHECK(cs.inamp[3] != 0 && !(cs.inamp[3] & 0x80), "усилитель входа смесителя без mute");
    /* громкость */
    hda_set_volume(50);
    CHECK((cs.amp[2] & 0x7F) == 0x20 && !(cs.amp[2] & 0x80), "громкость 50% = половина шагов ЦАП");
    hda_set_volume(100); CHECK((cs.amp[2] & 0x7F) == 0x40, "громкость 100%");
    hda_set_volume(0); CHECK((cs.amp[2] & 0x80) != 0, "громкость 0 = mute");
    hda_set_volume(70);
    /* воспроизведение */
    hda_play_chime();
    CHECK(hda_playing(), "звук запуска идёт");
    u32 ctl = sim_rd(0x100, 4);
    CHECK((ctl & 2) && ((ctl >> 20) & 0xF) == 1, "поток запущен с тегом 1");
    CHECK(sim_rd(0x100 + SD_CBL, 4) >= 36000 * 4 && (sim_rd(0x100 + SD_CBL, 4) % 128) == 0, "длина буфера кратна 128");
    u32 b0 = *(u32 *)(unsigned long)sim_rd(0x100 + SD_BDLPL, 4);
    CHECK(b0 == (u32)(unsigned long)pcm, "BDL указывает на буфер");
    int nz = 0, big = 0; for (int i = 0; i < 36000; i++) { if (pcm[i * 2]) nz++; if (pcm[i * 2] > 3000 || pcm[i * 2] < -3000) big++; }
    CHECK(nz > 30000 && big > 100 && pcm[0] == pcm[1], "в буфере слышимый стерео-звук");
    { extern void *fopen(const char *, const char *); extern unsigned long fwrite(const void *, unsigned long, unsigned long, void *); extern int fclose(void *); void *f = fopen("chime.raw", "wb"); if (f) { fwrite(pcm, 4, 36000, f); fclose(f); } }
    hda_poll(1000, 100);
    CHECK(hda_playing(), "в начале звук не обрывается");
    hda_poll(1000 + 100 * 2, 100);
    CHECK(!hda_playing() && !(sim_rd(0x100, 4) & 2), "по окончании поток остановлен");
    hda_play_pop(); CHECK(hda_playing(), "щелчок громкости");
    hda_play_chime(); CHECK(hda_playing(), "не перебиваем играющий звук");
    char d[100]; hda_diag(d, sizeof d); puts(d);
    printf("fails=%d\n", fails); return fails;
}
