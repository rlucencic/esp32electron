#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "electron.h"
#include "m6502.h"

#if !M6502_SLOW
#error "the Electron needs the 6502 core's slow pages: CONFIG_M6502_SLOW_PAGES (sdkconfig.defaults), -DM6502_SLOW_PAGES (host)"
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#define ELK_LOGI(...)   ESP_LOGI("elk", __VA_ARGS__)
#define ELK_LOGW(...)   ESP_LOGW("elk", __VA_ARGS__)
/* Buffers the CPU doesn't run in: PSRAM (the MaTouch's internal RAM is
 * short: esp32ql's lessons)
 */
#define ELK_SLOW_ALLOC(n) heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#include <stdio.h>
#include <stdlib.h>
#define ELK_LOGI(...)   do { printf("elk: " __VA_ARGS__); printf("\n"); } while (0)
#define ELK_LOGW(...)   do { printf("elk: warning: " __VA_ARGS__); printf("\n"); } while (0)
#define ELK_SLOW_ALLOC(n) calloc(1, (n))
#endif

/* ---- Time: 2 MHz ticks (the 6502 core's cycles) ----
 *
 * A line is 64 us, 128 ticks; the picture is interlaced: fields of 312 and
 * 313 lines alternate (50 Hz). Line 0 is the picture's first line, tick 0
 * of a line its first pixel; the picture is 256 lines (250 in modes 3 and
 * 6), the rest of the field border and blanking (docs/electron-hardware.md
 * 3.1).
 */
#define ELK_LINE_TICKS      128
#define ELK_CPU_HZ          2000000
#define ELK_DISPLAY_TICKS   80          /* a line's pixels and fetch: 40 us */
#define ELK_DISPLAY_END_TICK 96         /* display end: HS after the last line */
#define ELK_RTC_LINE        99          /* the 50 Hz interrupt: c 62 or -2 by the field */

/* The ULA's interrupt bits ($FE00) */
enum {
    INT_MASTER = 0x01,
    INT_POWER_ON = 0x02,
    INT_DISPLAY_END = 0x04,
    INT_RTC = 0x08,
    INT_RX_FULL = 0x10,
    INT_TX_EMPTY = 0x20,
    INT_HIGH_TONE = 0x40,
};

typedef struct {
    elk_config_t cfg;
    m6502_t cpu;
    uint8_t *ram;
    const uint8_t *basic, *mos, *adfs;

    uint64_t frame_start;               /* the tick line 0 began at */
    uint32_t field_ticks;               /* this field's length: 312 or 313 lines */
    int line;                           /* the line being run */
    uint32_t frames;

    /* The ULA */
    uint8_t int_status, int_enable;
    uint8_t rom_page;                   /* the ULA latch, $FE05 bits 0-3 */
    uint8_t external_page;             /* the expansion bus ROM latches */
    uint16_t screen_start;              /* $FE02/$FE03: the next frame's */
    uint8_t counter;                    /* $FE06 */
    uint8_t control;                    /* $FE07 */
    uint8_t palette[8];                 /* $FE08-$FE0F */
    uint8_t mode;                       /* $FE07 bits 3-5, 0-6 */

    /* Something the picture shows changed (a write to RAM at the mode's
     * screen base or above, the palette, the mode, the start; BASIC's stack
     * is just under the screen): video.c compares the
     * field's picture with the last one shown only for the two fields from
     * then (a write after its line was recorded shows in the next)
     */
    uint8_t screen_touched;
    uint16_t screen_base;               /* the mode's: $3000, $4000, $5800, $6000 */

    /* The keyboard: a bit per row (0-3), per column (0-13): 1 pressed */
    volatile uint8_t matrix[14];
    bool break_down;
} elk_t;

const uint8_t *elk_cart_bank(unsigned page);

extern elk_t elk;

static inline uint64_t elk_line_start(void)
{
    return elk.frame_start + (uint64_t)elk.line * ELK_LINE_TICKS;
}

/* Modes 0-3: no CPU RAM access during a pixel line's fetch */
static inline bool elk_contended_mode(void)
{
    return elk.mode < 4;
}

/* The picture's lines: 256, or 250 in modes 3 and 6 */
static inline int elk_display_lines(void)
{
    return (elk.mode == 3 || elk.mode == 6) ? 250 : 256;
}

/* ula.c */
void elk_ula_reset(bool power_on);
uint8_t elk_ula_read(uint16_t addr);
void elk_ula_write(uint16_t addr, uint8_t v);
void elk_ula_update_irq(void);
void elk_ula_raise(uint8_t bits);       /* interrupt conditions */
void elk_map_rom(void);                 /* $8000-$BFFF for the current ROM page */
void elk_map_ram(void);                 /* $0000-$7FFF for the mode */

/* video.c */
bool elk_video_init(void);
void elk_video_frame_start(void);
void elk_video_line(int line);          /* record a picture line */
void elk_video_frame_end(void);

/* sound.c */
void elk_sound_line(void);

/* keyboard.c */
uint8_t elk_keyboard_read(uint16_t addr);
void elk_keyboard_frame(void);

/* tape.c */
extern uint64_t elk_tape_event;
void elk_tape_advance(uint64_t now);
void elk_tape_control(void);
uint8_t elk_tape_data(void);

/* plus3.c */
bool elk_plus3_init(void);
void elk_plus3_reset(void);
void elk_plus3_update(void);
uint8_t elk_plus3_read(uint16_t address);
void elk_plus3_write(uint16_t address, uint8_t value);
