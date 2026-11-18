/*
 * The ULA's picture. The screen is in character rows, as on the BBC Micro:
 * a row is 8 lines (10 in modes 3 and 6, the last two blank), each line of
 * it one byte from every 8 (80 bytes a line in modes 0-3, 40 in 4-6).
 * Addresses past $7FFF wrap back by the mode's screen size (20 KB in modes
 * 0-2, 16 KB in 3, 10 KB in 4-5, 8 KB in 6). The start address is taken at
 * the frame's start.
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

static const uint16_t screen_size[7] = { 0x5000, 0x5000, 0x5000, 0x4000, 0x2800, 0x2800, 0x2000 };

bool elk_video_init(void)
{
    if (s_frames == NULL) {
        s_frames = ELK_SLOW_ALLOC(FRAMES * sizeof(frame_t));
    }
    return s_frames != NULL;
}

static inline uint16_t wrap(uint32_t a)
{
    return a >= 0x8000 ? a - screen_size[elk.mode] : a;
}

void elk_video_frame_start(void)
{
    s_row_addr = elk.screen_start;
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

/* The physical colours (RGB565), by the ULA's 3-bit colour: bit 0 red, 1
 * green, 2 blue
 */
static const uint16_t rgb[8] = { 0x0000, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF };

/* The logical colours of a line (16 in mode 2, 4 in 1 and 5, 2 in the
 * others) from its palette registers. PROVISIONAL: the defaults the MOS
 * sets, until the registers' bit layout is in (docs/electron-hardware.md).
 */
static void colours(const line_rec_t *r, uint16_t *out)
{
    switch (r->mode) {
    case 2:
        for (int i = 0; i < 16; i++) {
            out[i] = rgb[i & 7];
        }
        break;
    case 1: case 5:
        out[0] = rgb[0];
        out[1] = rgb[1];
        out[2] = rgb[3];
        out[3] = rgb[7];
        break;
    default:
        out[0] = rgb[0];
        out[1] = rgb[7];
        break;
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
    case 0: case 3:                     /* 640 pixels, 2 colours */
        for (int i = 0; i < 80; i++) {
            for (int b = 7; b >= 0; b--) {
                *dst++ = c[(p[i] >> b) & 1];
            }
        }
        break;
    case 4: case 6:                     /* 320, 2 colours */
        for (int i = 0; i < 40; i++) {
            for (int b = 7; b >= 0; b--) {
                uint16_t v = c[(p[i] >> b) & 1];
                *dst++ = v;
                *dst++ = v;
            }
        }
        break;
    case 1: case 5: {                   /* 320 or 160, 4 colours: bits 7 and 3 the first pixel */
        int n = r->mode == 1 ? 80 : 40, w = r->mode == 1 ? 2 : 4;
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < 4; k++) {
                uint16_t v = c[(((p[i] >> (7 - k)) & 1) << 1) | ((p[i] >> (3 - k)) & 1)];
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
