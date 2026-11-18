#pragma once

/*
 * Acorn Electron: a 6502A at 2 MHz (1 MHz to RAM and the ULA), 32 KB RAM,
 * the ULA (display, sound, cassette, keyboard, interrupts, ROM paging),
 * BBC BASIC II and the MOS in a 32 KB ROM; an Acorn Plus 3 (ADFS, a WD1770
 * and a 3.5" drive) as an option. Platform independent: the board's main.c
 * and the host build (host/) drive it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ELK_OS_BASIC_SIZE       (32 * 1024)     /* BASIC $8000-$BFFF, then the MOS */
#define ELK_PAGED_ROM_SIZE      (16 * 1024)
#define ELK_RAM_SIZE            (32 * 1024)

/* The picture: 640 x 256 (modes with fewer pixels drawn wider) */
#define ELK_SCREEN_WIDTH        640
#define ELK_SCREEN_HEIGHT       256
#define ELK_SCREEN_HZ           50

/* Sound: 16-bit stereo */
#define ELK_AUDIO_HZ            31250

typedef struct {
    const uint8_t *os_basic;    /* ELK_OS_BASIC_SIZE bytes */
    uint8_t *ram;               /* ELK_RAM_SIZE bytes */
    /* The Plus 3's ADFS ROM (ELK_PAGED_ROM_SIZE bytes); NULL: no Plus 3 */
    const uint8_t *adfs;
} elk_config_t;

bool elk_init(const elk_config_t *config);
void elk_reset(void);                   /* power off and on */
void elk_run_frame(void);
bool elk_halted(void);

/* ---- Display ---- */
uint32_t elk_screen_signature(void);
void elk_render_line_rgb565(int y, uint16_t *dst);

/* ---- Keyboard: keys by matrix position (column * 4 + row) ---- */
#define ELK_KEYS                (14 * 4)
void elk_key(int key, bool down);       /* from any task */
void elk_break(bool down);              /* the BREAK key: the 6502's reset */
bool elk_type(const char *text);        /* \n is RETURN; false while still typing */

/* ---- Sound ---- */
void elk_audio_enable(bool on);
int elk_audio_render(int16_t *dst, int frames);
int elk_audio_pending(void);

/* ---- Status ---- */
typedef struct {
    uint32_t frames;
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint8_t mode;               /* the display mode, 0-6 */
    uint8_t rom_page;           /* $FE05's ROM number */
    bool caps_lock;             /* the CAPS LK light */
    bool motor;                 /* the cassette motor */
} elk_status_t;

void elk_get_status(elk_status_t *s);
