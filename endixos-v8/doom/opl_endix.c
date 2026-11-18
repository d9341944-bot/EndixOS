/* Драйвер OPL для EndixOS: эмулятор Nuked OPL3 (LGPL) + очередь отложенных вызовов по образцу opl_sdl.c Chocolate Doom,
 * но без SDL: сэмплы забирает наш микшер (i_endixsound.c) функцией OPL_Endix_Generate. Без плавающей точки. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opl3.h"
#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"

typedef struct { unsigned int rate, enabled, value; uint64_t expire_time; } opl_timer_t;

static opl_callback_queue_t *callback_queue;
static uint64_t current_time;
static int opl_paused;
static uint64_t pause_offset;
static opl3_chip opl_chip;
static int register_num;
static opl_timer_t timer1 = { 12500, 0, 0, 0 }, timer2 = { 3125, 0, 0, 0 };
static int mixing_freq;
static int ready;

static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback; void *callback_data;
    uint64_t us = ((uint64_t) nsamples * OPL_SECOND) / mixing_freq;
    current_time += us;
    if (opl_paused) pause_offset += us;
    while (!OPL_Queue_IsEmpty(callback_queue) && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data)) break;
        callback(callback_data);
    }
}

/* nframes стерео-кадров на частоте opl_sample_rate в out (int16, 2 канала) */
void OPL_Endix_Generate(int16_t *out, unsigned int nframes)
{
    unsigned int filled = 0;
    if (!ready) { memset(out, 0, nframes * 4); return; }
    while (filled < nframes)
    {
        uint64_t nsamples;
        if (opl_paused || OPL_Queue_IsEmpty(callback_queue)) nsamples = nframes - filled;
        else
        {
            uint64_t next = OPL_Queue_Peek(callback_queue) + pause_offset;
            nsamples = next > current_time ? (next - current_time) * mixing_freq : 0;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;
            if (nsamples > nframes - filled) nsamples = nframes - filled;
        }
        if (nsamples) OPL3_GenerateStream(&opl_chip, out + filled * 2, (uint32_t) nsamples);
        filled += (unsigned int) nsamples;
        AdvanceTime((unsigned int) nsamples);
    }
}

void OPL_Endix_Skip(uint64_t us)
{
    if (ready) AdvanceTime((unsigned int) ((us * (uint64_t) mixing_freq) / OPL_SECOND) + 1);
}

static void OPL_Endix_Shutdown(void)
{
    if (ready) { OPL_Queue_Destroy(callback_queue); ready = 0; }
}

static int OPL_Endix_Init(unsigned int port_base)
{
    (void) port_base;
    mixing_freq = (int) opl_sample_rate;
    opl_paused = 0; pause_offset = 0; current_time = 0; register_num = 0;
    timer1.enabled = timer2.enabled = 0;
    callback_queue = OPL_Queue_Create();
    OPL3_Reset(&opl_chip, (uint32_t) mixing_freq);
    ready = 1;
    return 1;
}

static unsigned int OPL_Endix_PortRead(opl_port_t port)
{
    unsigned int result = 0;
    if (port == OPL_REGISTER_PORT_OPL3) return 0xff;
    if (timer1.enabled && current_time > timer1.expire_time) result |= 0xc0;
    if (timer2.enabled && current_time > timer2.expire_time) result |= 0xa0;
    return result;
}

static void TimerEnd(opl_timer_t *t)
{
    if (t->enabled) t->expire_time = current_time + ((uint64_t) (0x100 - t->value) * OPL_SECOND) / t->rate;
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1: timer1.value = value; TimerEnd(&timer1); break;
        case OPL_REG_TIMER2: timer2.value = value; TimerEnd(&timer2); break;
        case OPL_REG_TIMER_CTRL:
            if (value & 0x80) { timer1.enabled = 0; timer2.enabled = 0; }
            else {
                if ((value & 0x40) == 0) { timer1.enabled = (value & 1) != 0; TimerEnd(&timer1); }
                if ((value & 0x20) == 0) { timer2.enabled = (value & 2) != 0; TimerEnd(&timer2); }
            }
            break;
        default: OPL3_WriteRegBuffered(&opl_chip, (uint16_t) reg_num, (uint8_t) value); break;
    }
}

static void OPL_Endix_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT) register_num = (int) value;
    else if (port == OPL_REGISTER_PORT_OPL3) register_num = (int) (value | 0x100);
    else if (port == OPL_DATA_PORT) WriteRegister((unsigned) register_num, value);
}

static void OPL_Endix_SetCallback(uint64_t us, opl_callback_t callback, void *data)
{ OPL_Queue_Push(callback_queue, callback, data, current_time - pause_offset + us); }
static void OPL_Endix_ClearCallbacks(void) { OPL_Queue_Clear(callback_queue); }
static void OPL_Endix_Lock(void) { }
static void OPL_Endix_Unlock(void) { }
static void OPL_Endix_SetPaused(int paused) { opl_paused = paused; }
static void OPL_Endix_AdjustCallbacks(unsigned num, unsigned den) { OPL_Queue_AdjustCallbacks(callback_queue, current_time, num, den); }

opl_driver_t opl_endix_driver =
{
    "EndixOS", OPL_Endix_Init, OPL_Endix_Shutdown, OPL_Endix_PortRead, OPL_Endix_PortWrite,
    OPL_Endix_SetCallback, OPL_Endix_ClearCallbacks, OPL_Endix_Lock, OPL_Endix_Unlock,
    OPL_Endix_SetPaused, OPL_Endix_AdjustCallbacks,
};

int OPL_Endix_Active(void) { return ready; }
