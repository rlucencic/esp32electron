/*
 * The Electron: set up, reset, the frame loop and the 6502's bus.
 *
 * Time is counted in 2 MHz ticks, the 6502 core's cycles. ROM reads run at
 * 2 MHz (a tick: the core's page pointers, no callback). RAM and the
 * ULA's registers run at 1 MHz: an access waits for the 1 MHz clock's
 * phase and takes two ticks (bus callbacks add the wait). In modes 0-3 the
 * ULA reads the screen at 2 MHz during each picture line's fetch, and a
 * CPU access to RAM then waits for the fetch's end.
 * (docs/electron-hardware.md has the sources.)
 */
#include <string.h>
#include "elk_internal.h"

elk_t elk;

static uint8_t s_ff_page[256];          /* nothing there: $FF */

/* ---- Memory map ---- */

/* $8000-$BFFF for the ROM page in $FE05: 8-9 the keyboard (read through
 * the bus callback), 10-11 BASIC, 4 the Plus 3's ADFS, the rest empty
 */
void elk_map_rom(void)
{
    const uint8_t *rom = NULL;
    switch (elk.rom_page) {
    case 8: case 9:
        for (int p = 0x80; p < 0xC0; p++) {
            elk.cpu.rd[p] = NULL;
        }
        return;
    case 10: case 11:
        rom = elk.basic;
        break;
    case 4:
        rom = elk.adfs;
        break;
    default:
        break;
    }
    for (int p = 0x80; p < 0xC0; p++) {
        elk.cpu.rd[p] = rom ? (uint8_t *)rom + (p - 0x80) * 256 : s_ff_page;
    }
}

static void map_memory(void)
{
    for (int p = 0; p < 256; p++) {
        elk.cpu.rd[p] = NULL;
        elk.cpu.wr[p] = NULL;
    }
    /* RAM ($0000-$7FFF) through the callbacks: its timing */
    for (int p = 0xC0; p < 0x100; p++) {
        if (p < 0xFC || p == 0xFF) {
            elk.cpu.rd[p] = (uint8_t *)elk.mos + (p - 0xC0) * 256;
        }
    }
    elk_map_rom();
}

/* ---- Bus timing ---- */

/* A RAM access starting at tick t: past the fetch (modes 0-3), then on the
 * 1 MHz clock; the access is the two ticks from the returned one
 */
static inline uint64_t ram_slot(uint64_t t)
{
    if (elk_contended_mode()) {
        uint64_t pos = (t - elk.frame_start) % ELK_FRAME_TICKS;
        uint32_t line = pos / ELK_LINE_TICKS, p = pos % ELK_LINE_TICKS;
        bool fetching = (int)line < elk_display_lines() && (elk.mode != 3 || line % 10 < 8);
        if (fetching && p < ELK_DISPLAY_TICKS) {
            t += ELK_DISPLAY_TICKS - p;
        }
    }
    return (t + 1) & ~(uint64_t)1;
}

/* The core adds the access's last tick after the callback */
static inline void wait_ram(m6502_t *c)
{
    c->cycles = ram_slot(c->cycles) + 1;
}

/* The ULA's registers: 1 MHz, no contention */
static inline void wait_io(m6502_t *c)
{
    c->cycles = ((c->cycles + 1) & ~(uint64_t)1) + 1;
}

static uint8_t bus_read(void *ctx, uint16_t a)
{
    m6502_t *c = &elk.cpu;
    if (a < 0x8000) {
        wait_ram(c);
        return elk.ram[a];
    }
    if (a < 0xC000) {
        wait_io(c);                     /* the keyboard: 1 MHz (MAME) */
        return elk_keyboard_read(a);    /* (only the keyboard pages come here) */
    }
    if (a >= 0xFE00 && a < 0xFF00) {
        wait_io(c);
        return elk_ula_read(a);
    }
    if (a >= 0xFC00 && a < 0xFE00) {
        wait_io(c);
        return 0xFF;                    /* the expansion bus: nothing yet */
    }
    return elk.mos[a - 0xC000];
}

#ifdef ELK_HOST_HOOK
void elk_host_write(uint16_t a, uint8_t v);     /* host builds: ELK_WATCH */
#endif

static void bus_write(void *ctx, uint16_t a, uint8_t v)
{
    m6502_t *c = &elk.cpu;
#ifdef ELK_HOST_HOOK
    elk_host_write(a, v);
#endif
    if (a < 0x8000) {
        wait_ram(c);
        elk.ram[a] = v;
        return;
    }
    if (a >= 0xFE00 && a < 0xFF00) {
        wait_io(c);
        elk_ula_write(a, v);
        return;
    }
    if (a >= 0xFC00 && a < 0xFE00) {
        wait_io(c);
    }
    /* ROM: nothing */
}

/* ---- Set up and reset ---- */

bool elk_init(const elk_config_t *config)
{
    memset(&elk, 0, sizeof(elk));
    elk.cfg = *config;
    elk.ram = config->ram;
    elk.basic = config->os_basic;
    elk.mos = config->os_basic + 0x4000;
    elk.adfs = config->adfs;
    memset(s_ff_page, 0xFF, sizeof(s_ff_page));
    if (!elk_video_init()) {
        return false;
    }
    const m6502_bus_t bus = { bus_read, bus_write, NULL, NULL };
    m6502_init(&elk.cpu, &bus);
    elk.cpu.decimal = true;
    elk.cpu.stop_pc = 0x10000;
    elk_reset();
    return true;
}

void elk_reset(void)
{
    memset(elk.ram, 0, ELK_RAM_SIZE);
    elk.frame_start = elk.cpu.cycles;
    elk_ula_reset(true);
    map_memory();
    m6502_reset(&elk.cpu);
}

bool elk_halted(void)
{
    return elk.cpu.jammed;
}

/* BREAK holds the 6502 in reset; it starts again as the key comes up */
void elk_break(bool down)
{
    elk.break_down = down;
}

/* ---- The frame ---- */

void elk_run_frame(void)
{
    static bool was_down;
    elk_video_frame_start();
    for (elk.line = 0; elk.line < ELK_LINES; elk.line++) {
        elk_ula_line();
        elk_video_line(elk.line);
        uint64_t end = elk_line_start() + ELK_LINE_TICKS;
        if (elk.break_down) {
            if (elk.cpu.cycles < end) {
                elk.cpu.cycles = end;
            }
        } else {
            if (was_down) {
                elk_ula_reset(false);
                map_memory();
                m6502_reset(&elk.cpu);
            }
            elk.cpu.until = end;
            m6502_run(&elk.cpu);
        }
        was_down = elk.break_down;
        elk_sound_line();
    }
    elk.frame_start += ELK_FRAME_TICKS;
    elk.frames++;
    elk_video_frame_end();
    elk_keyboard_frame();
}

void elk_get_status(elk_status_t *s)
{
    s->frames = elk.frames;
    s->pc = elk.cpu.pc;
    s->a = elk.cpu.a;
    s->x = elk.cpu.x;
    s->y = elk.cpu.y;
    s->s = elk.cpu.s;
    s->p = elk.cpu.p;
    s->mode = elk.mode;
    s->rom_page = elk.rom_page;
    s->caps_lock = (elk.control & 0x80) != 0;
    s->motor = (elk.control & 0x40) != 0;
}
