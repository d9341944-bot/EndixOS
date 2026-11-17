/* Звуковые эффекты Doom для EndixOS: микшер на 16 голосов поверх циклического кольца, которое крутит HDA.
 * Голос без состояния: отсчёт = (кадр - начало) * шаг, поэтому кольцо можно домешивать заново с любого места. */
#include <stdlib.h>
#include <string.h>
#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doom_sys.h"

#define RING   32768u                     /* кадров: 0,68 с */
#define AHEAD  6000u                      /* насколько вперёд домешиваем (125 мс) */
#define MINLAT 1200u                      /* новый звук стартует не раньше, чем через 25 мс */
#define NV     16

typedef struct { const uint8_t *s; uint32_t len, step; uint32_t start; int lv, rv, active; } voice_t;
static voice_t vo[NV];
static int16_t ring[RING * 2] __attribute__((aligned(128)));
static int running, use_prefix;
static uint32_t wpos, ppos, lastp;       /* абсолютные кадры: записано, играется, позиция в кольце */
static uint32_t last_ms;

static void clear_ring(void) { memset(ring, 0, sizeof ring); }

static void mix_to(uint32_t upto)
{
    while (wpos != upto) {
        int l = 0, r = 0;
        for (int i = 0; i < NV; i++) {
            voice_t *v = &vo[i];
            if (!v->active || (int32_t)(wpos - v->start) < 0) continue;
            uint32_t idx = (uint32_t)(((uint64_t)(wpos - v->start) * v->step) >> 16);
            if (idx >= v->len) continue;
            int smp = (int)v->s[idx] - 128;
            l += smp * v->lv; r += smp * v->rv;
        }
        l >>= 1; r >>= 1;                                        /* запас от перегрузки при многих голосах */
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        uint32_t o = (wpos % RING) * 2;
        ring[o] = (int16_t)l; ring[o + 1] = (int16_t)r;
        wpos++;
    }
}

void doom_snd_resume(void)
{
    if (!running) return;
    clear_ring(); wpos = ppos = lastp = 0;
    if (!doom_audio_start(ring, RING)) { /* звука нет: молча */ }
    last_ms = 0;
}
void doom_snd_stop(void) { doom_audio_stop(); }

void doom_snd_pump(void)
{
    if (!running) return;
    uint32_t p = doom_audio_pos() % RING;
    uint32_t ms = doom_now_ms();
    if (last_ms && ms - last_ms > 450) {                         /* долгий провал (загрузка уровня): кольцо уже проиграно с мусором */
        clear_ring(); ppos += (p + RING - lastp) % RING; wpos = ppos;
    } else ppos += (p + RING - lastp) % RING;
    lastp = p; last_ms = ms;
    if ((int32_t)(wpos - ppos) < 0) wpos = ppos;                 /* отстали: дальше тишина */
    mix_to(ppos + AHEAD);
    for (int i = 0; i < NV; i++) if (vo[i].active && vo[i].len && (uint32_t)(((uint64_t)(wpos - vo[i].start) * vo[i].step) >> 16) >= vo[i].len && (int32_t)(wpos - vo[i].start) > 0) vo[i].active = 0;
}

static boolean Snd_Init(boolean prefix)
{
    use_prefix = prefix; running = 1;
    doom_snd_resume();
    return true;
}
static void Snd_Shutdown(void) { running = 0; doom_audio_stop(); }
static int Snd_GetLump(sfxinfo_t *s)
{
    char nm[16];
    if (use_prefix) { nm[0] = 'd'; nm[1] = 's'; strncpy(nm + 2, s->name, 12); nm[13] = 0; }
    else { strncpy(nm, s->name, 12); nm[12] = 0; }
    return W_GetNumForName(nm);
}
static void Snd_Update(void) { }
static void set_lr(voice_t *v, int vol, int sep)
{
    if (vol < 0) vol = 0; if (vol > 127) vol = 127;
    if (sep < 0) sep = 0; if (sep > 254) sep = 254;
    v->lv = ((254 - sep) * vol) / 127; v->rv = (sep * vol) / 127;
}
static void Snd_UpdateParams(int ch, int vol, int sep) { if (ch >= 0 && ch < NV) set_lr(&vo[ch], vol, sep); }
static int Snd_Start(sfxinfo_t *s, int ch, int vol, int sep)
{
    if (ch < 0 || ch >= NV || !running) return -1;
    int lump = s->lumpnum;
    int size = W_LumpLength(lump);
    const uint8_t *d = W_CacheLumpNum(lump, PU_STATIC);
    if (size < 48 || d[0] != 3 || d[1] != 0) return -1;
    uint32_t rate = d[2] | (d[3] << 8), len = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t)d[7] << 24);
    if (len > (uint32_t)size - 8 || len <= 48 || rate == 0) return -1;
    voice_t *v = &vo[ch];
    v->s = d + 8 + 16; v->len = len - 32;                        /* DMX пропускает по 16 байт в начале и в конце */
    v->step = (uint32_t)(((uint64_t)rate << 16) / 48000u);
    set_lr(v, vol, sep);
    v->start = ppos + MINLAT; v->active = 1;
    if ((int32_t)(wpos - v->start) > 0) wpos = v->start;         /* перемешать вперёд от старта нового голоса */
    return ch;
}
static void Snd_Stop(int ch)
{
    if (ch < 0 || ch >= NV || !vo[ch].active) return;
    vo[ch].active = 0;
    uint32_t t = ppos + MINLAT; if ((int32_t)(wpos - t) > 0) wpos = t;
}
static boolean Snd_IsPlaying(int ch) { return ch >= 0 && ch < NV && vo[ch].active; }
static void Snd_Cache(sfxinfo_t *s, int n) { (void)s; (void)n; }

static snddevice_t devs[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32 };
sound_module_t DG_sound_module = { devs, 6, Snd_Init, Snd_Shutdown, Snd_GetLump, Snd_Update, Snd_UpdateParams, Snd_Start, Snd_Stop, Snd_IsPlaying, Snd_Cache };
