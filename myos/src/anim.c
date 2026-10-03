#include "anim.h"

struct anim_entry {
    int      active;
    uint32_t elapsed;   /* в тиках */
    uint32_t duration;  /* в тиках */
};

static struct anim_entry anims[ANIM_MAX];

void anim_init(void) {
    for (int i = 0; i < ANIM_MAX; i++) anims[i].active = 0;
}

void anim_start(int id, uint32_t duration_ticks) {
    if (id < 0 || id >= ANIM_MAX) return;
    anims[id].active   = 1;
    anims[id].elapsed  = 0;
    anims[id].duration = duration_ticks;
}

float anim_progress(int id) {
    if (id < 0 || id >= ANIM_MAX) return 1.0f;
    if (!anims[id].active) return 1.0f;
    if (anims[id].duration == 0) return 1.0f;
    float t = (float)anims[id].elapsed / (float)anims[id].duration;
    if (t > 1.0f) t = 1.0f;
    return t;
}

int anim_active(int id) {
    if (id < 0 || id >= ANIM_MAX) return 0;
    return anims[id].active;
}

void anim_tick(void) {
    for (int i = 0; i < ANIM_MAX; i++) {
        if (!anims[i].active) continue;
        anims[i].elapsed++;
        if (anims[i].elapsed >= anims[i].duration)
            anims[i].active = 0;
    }
}

float ease_out_cubic(float t) {
    float u = 1.0f - t;
    return 1.0f - u*u*u;
}
float ease_in_quad(float t) {
    return t * t;
}
float ease_in_out_cubic(float t) {
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u*u*u / 2.0f;
}
