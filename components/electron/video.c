/*
 * The ULA's picture. The screen is in character rows, as on the BBC Micro:
 * a row is 8 lines (10 in modes 3 and 6, the last two blank), each line of
 * it one byte from every 8 (80 bytes a line in modes 0-3, 40 in 4-6).
 * The address counter is 15 bits: one that comes to $0000-$07FF (past
 * $7FFF, or a start that low) goes on at the mode's base ($3000 in modes
 * 0-2, $4000 in 3, $5800 in 4-5, $6000 in 6) plus its low 11 bits. The
 * start address is taken at the field's start.
 *
 * Each picture line is recorded as it starts (its mode, palette and bytes)
 * into one of three frames; a finished frame that differs from the last one
 * shown becomes the one the presenter (another task) draws.
 */
#include <string.h>
#include "elk_internal.h"

#define FRAMES          3
#define LINE_BYTES      80

typedef struct {
    uint8_t mode;
    bool blank;
    uint8_t palette[8];
    uint8_t bytes[LINE_BYTES];
} line_rec_t;

typedef struct {
    line_rec_t lines[ELK_SCREEN_HEIGHT];
} frame_t;

static frame_t *s_frames;
static int s_writing = 1;
static volatile int s_latest;
static volatile uint32_t s_signature;

static uint16_t s_row_addr;             /* the character row being fetched */
static int s_row_line;                  /* its line */

static const uint16_t mode_base[7] = { 0x3000, 0x3000, 0x3000, 0x4000, 0x5800, 0x5800, 0x6000 };

bool elk_video_init(void)
{
    if (s_frames == NULL) {
        s_frames = ELK_SLOW_ALLOC(FRAMES * sizeof(frame_t));
    }
    return s_frames != NULL;
}

static inline uint16_t wrap(uint32_t a)
{
    a &= 0x7FFF;
    return a < 0x0800 ? mode_base[elk.mode] + a : a;
}

void elk_video_frame_start(void)
{
    s_row_addr = wrap(elk.screen_start);
    s_row_line = 0;
}

void elk_video_line(int line)
{
    if (line >= ELK_SCREEN_HEIGHT || s_frames == NULL) {
        return;
    }
    line_rec_t *r = &s_frames[s_writing].lines[line];
    int mode = elk.mode;
    int lines_per_row = (mode == 3 || mode == 6) ? 10 : 8;
    int n = mode < 4 ? 80 : 40;
    r->mode = mode;
    memcpy(r->palette, elk.palette, sizeof(r->palette));
    r->blank = line >= elk_display_lines() || s_row_line >= 8;
    if (!r->blank) {
        for (int c = 0; c < n; c++) {
            r->bytes[c] = elk.ram[wrap(s_row_addr + c * 8 + s_row_line)];
        }
    }
    if (++s_row_line >= lines_per_row) {
        s_row_line = 0;
        s_row_addr = wrap(s_row_addr + n * 8);
    }
}

void elk_video_frame_end(void)
{
    if (s_frames == NULL) {
        return;
    }
    if (memcmp(&s_frames[s_writing], &s_frames[s_latest], sizeof(frame_t)) == 0) {
        return;                         /* the same picture: keep writing this one */
    }
    s_latest = s_writing;
    s_signature++;
    int next = (s_writing + 1) % FRAMES;
    s_writing = next;
}

uint32_t elk_screen_signature(void)
{
    return s_signature;
}

/* ---- Drawing ---- */

/* The palette ($FE08-$FE0F): a pixel's 4-bit index (2 colours: 0 or 8;
 * 4 colours: 0, 2, 8, 10; 16: all) to one of 8 colours, a bit a gun, 1
 * turning it off (docs/electron-hardware.md 3.4). Each register pair holds
 * four indices a, b, c, d:
 *   even: B(a) B(b) B(c) B(d) G(a) G(b) x x      odd: x x G(c) G(d) R(a) R(b) R(c) R(d)
 */
static void colours(const line_rec_t *r, uint16_t *out)
{
    static const uint8_t group[4][4] = { { 10, 8, 2, 0 }, { 14, 12, 6, 4 }, { 15, 13, 7, 5 }, { 11, 9, 3, 1 } };
    for (int g = 0; g < 4; g++) {
        uint8_t even = ~r->palette[2 * g], odd = ~r->palette[2 * g + 1];
        for (int k = 0; k < 4; k++) {
            bool red = (odd >> (3 - k)) & 1;
            bool green = k < 2 ? (even >> (3 - k)) & 1 : (odd >> (7 - k)) & 1;
            bool blue = (even >> (7 - k)) & 1;
            out[group[g][k]] = (red ? 0xF800 : 0) | (green ? 0x07E0 : 0) | (blue ? 0x001F : 0);
        }
    }
}

void elk_render_line_rgb565(int y, uint16_t *dst)
{
    if (s_frames == NULL || y < 0 || y >= ELK_SCREEN_HEIGHT) {
        return;
    }
    const line_rec_t *r = &s_frames[s_latest].lines[y];
    if (r->blank) {
        memset(dst, 0, ELK_SCREEN_WIDTH * sizeof(uint16_t));
        return;
    }
    uint16_t c[16];
    colours(r, c);
    const uint8_t *p = r->bytes;
    switch (r->mode) {
    case 0: case 3:                     /* 640 pixels, 2 colours: indices 0 and 8 */
        for (int i = 0; i < 80; i++) {
            for (int b = 7; b >= 0; b--) {
                *dst++ = c[((p[i] >> b) & 1) << 3];
            }
        }
        break;
    case 4: case 6:                     /* 320, 2 colours */
        for (int i = 0; i < 40; i++) {
            for (int b = 7; b >= 0; b--) {
                uint16_t v = c[((p[i] >> b) & 1) << 3];
                *dst++ = v;
                *dst++ = v;
            }
        }
        break;
    case 1: case 5: {                   /* 320 or 160, 4 colours: bits 7 and 3 the first pixel */
        int n = r->mode == 1 ? 80 : 40, w = r->mode == 1 ? 2 : 4;
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < 4; k++) {
                uint16_t v = c[(((p[i] >> (7 - k)) & 1) << 3) | (((p[i] >> (3 - k)) & 1) << 1)];
                for (int j = 0; j < w; j++) {
                    *dst++ = v;
                }
            }
        }
        break;
    }
    case 2:                             /* 160, 16 colours: bits 7 5 3 1 the first pixel */
        for (int i = 0; i < 80; i++) {
            for (int k = 0; k < 2; k++) {
                uint8_t b = p[i] << k;
                uint16_t v = c[((b >> 4) & 8) | ((b >> 3) & 4) | ((b >> 2) & 2) | ((b >> 1) & 1)];
                for (int j = 0; j < 4; j++) {
                    *dst++ = v;
                }
            }
        }
        break;
    }
}
