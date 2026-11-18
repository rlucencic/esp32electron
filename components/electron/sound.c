/*
 * Sound: with $FE07's bits 1-2 at 01 the ULA makes a square wave of
 * 1 MHz / 16 / (counter + 1) (the counter is $FE06). 16-bit stereo at
 * ELK_AUDIO_HZ, two samples each line (15625 lines a second), each the
 * wave's average over its half line, into a ring the board's speaker task
 * drains (which paces the emulation: the runtime waits while two frames are
 * queued; silence is queued too).
 */
#include <string.h>
#include "elk_internal.h"

#define RING            8192            /* stereo frames, a power of two */
#define LEVEL           6000

static int16_t s_ring[RING][2];
static volatile uint32_t s_head, s_tail;
static uint32_t s_phase;                /* ticks into the wave's period */

/* Ticks of a sample window the wave is high, the phase moved on */
static int high_ticks(int window)
{
    uint32_t half = 16u * (elk.counter + 1);    /* half a period, 2 MHz ticks */
    uint32_t period = 2 * half;
    int high = 0;
    while (window > 0) {
        s_phase %= period;
        uint32_t left = s_phase < half ? half - s_phase : period - s_phase;
        int step = (int)left < window ? (int)left : window;
        if (s_phase < half) {
            high += step;
        }
        s_phase += step;
        window -= step;
    }
    return high;
}

void elk_sound_line(void)
{
    bool on = ((elk.control >> 1) & 3) == 1;
    for (int i = 0; i < 2; i++) {
        int16_t level = 0;
        if (on) {
            int window = ELK_LINE_TICKS / 2;
            level = (int16_t)((2 * high_ticks(window) - window) * LEVEL / window);
        }
        uint32_t head = s_head;
        if (head - s_tail >= RING) {
            continue;                   /* nobody drains it (the host without a WAV) */
        }
        s_ring[head % RING][0] = level;
        s_ring[head % RING][1] = level;
        s_head = head + 1;
    }
}

void elk_audio_enable(bool on)
{
    if (!on) {
        s_tail = s_head;
    }
}

int elk_audio_render(int16_t *dst, int frames)
{
    uint32_t tail = s_tail, n = s_head - tail;
    if (n > (uint32_t)frames) {
        n = frames;
    }
    for (uint32_t i = 0; i < n; i++) {
        dst[2 * i] = s_ring[(tail + i) % RING][0];
        dst[2 * i + 1] = s_ring[(tail + i) % RING][1];
    }
    s_tail = tail + n;
    return n;
}

int elk_audio_pending(void)
{
    return s_head - s_tail;
}
