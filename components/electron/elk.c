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
static uint8_t *s_dfs_ram;

static bool has_fdc(void)
{
    return elk.adfs || elk.cfg.dfs;
}

/* ---- Memory map ---- */

/* The ULA supplies the keyboard and BASIC. The expansions select
 * the other ROMs through their own latches.
 */
void elk_map_rom(void)
{
    const uint8_t *rom = NULL;
    bool dfs_ram = false;
    unsigned mask = ELK_PAGED_ROM_SIZE - 1;
    for (int p = 0x80; p < 0xC0; p++) {
        elk.cpu.wr[p] = NULL;
    }
    switch (elk.rom_page) {
    case 8: case 9:
        for (int p = 0x80; p < 0xC0; p++) {
            elk.cpu.rd[p] = NULL;
        }
        return;
    case 10: case 11:
        rom = elk.basic;
        break;
    default:
        if (elk.cfg.dfs && elk.external_page == 2) {
            rom = elk.cfg.dfs;
            dfs_ram = true;
        } else if (elk.external_page < 4 && !(elk.cfg.dfs && elk.external_page == 3)) {
            rom = elk_cart_bank(elk.external_page);
        } else if (elk.external_page == 4) {
            rom = elk.adfs;
        } else if (elk.external_page == 12) {
            rom = elk.cfg.plus1;
            mask = ELK_PLUS1_ROM_SIZE - 1;
        }
        break;
    }
    for (int p = 0x80; p < 0xC0; p++) {
        if (dfs_ram && p >= 0xB0) {
            elk.cpu.rd[p] = elk.cpu.wr[p] = s_dfs_ram + (p - 0xB0) * 256;
        } else {
            elk.cpu.rd[p] = rom ? (uint8_t *)rom + (((p - 0x80) * 256) & mask) : s_ff_page;
        }
    }
}

/* RAM ($0000-$7FFF) for the mode: in modes 0-3 through the callbacks
 * (their timing, the contention); in 4-6, which never stall, the pages
 * below the screen through the core's pointers as slow pages (1 MHz: the
 * next even tick, two ticks), the screen's still through the callbacks
 * (they see what changes the picture)
 */
void elk_map_ram(void)
{
    for (int p = 0; p < 0x80; p++) {
        bool direct = !elk_contended_mode() && p < (elk.screen_base >> 8);
        elk.cpu.rd[p] = direct ? elk.ram + p * 256 : NULL;
        elk.cpu.wr[p] = direct ? elk.ram + p * 256 : NULL;
        elk.cpu.slow[p] = direct;
    }
}

static void map_memory(void)
{
    for (int p = 0; p < 256; p++) {
        elk.cpu.rd[p] = NULL;
        elk.cpu.wr[p] = NULL;
    }
    elk_map_ram();
    for (int p = 0xC0; p < 0x100; p++) {
        if (p < 0xFC || p == 0xFF) {
            elk.cpu.rd[p] = (uint8_t *)elk.mos + (p - 0xC0) * 256;
        }
    }
    elk_map_rom();
}

/* ---- Bus timing ---- */

/* A RAM access starting at tick t: the next 1 MHz slot (an even tick: 2
 * ticks from an even start, 3 from an odd one); in modes 0-3 a slot in a
 * pixel line's first 80 ticks is the ULA's, and the access waits for the
 * one at tick 80 (docs/electron-hardware.md 4.4: up to 41.5 us). Pixel
 * lines: 0-255, in mode 3 lines 0-7 of each 10-line row. Returns the
 * access's first tick.
 */
static inline uint64_t ram_slot(uint64_t t)
{
    t = (t + 1) & ~(uint64_t)1;
    if (elk_contended_mode()) {
        uint64_t pos = t - elk.frame_start;
        if (pos >= elk.field_ticks) {
            pos -= elk.field_ticks;     /* (run past the field's end: the next one's) */
        }
        uint32_t line = pos / ELK_LINE_TICKS, p = pos % ELK_LINE_TICKS;
        bool pixels = (int)line < elk_display_lines() && (elk.mode != 3 || line % 10 < 8);
        if (pixels && p < ELK_DISPLAY_TICKS) {
            t += ELK_DISPLAY_TICKS - p;
        }
    }
    return t;
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
        if (elk.cfg.plus1 && a == 0xFC72) {
            return 0x3F;               /* ADC ready, fire buttons released, printer idle */
        }
        return has_fdc() && (a & 0xFFF0) == 0xFCC0 ? elk_plus3_read(a) : 0xFF;
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
        if (a >= elk.screen_base && elk.ram[a] != v) {
            elk.screen_touched = 2;     /* (the MOS rewrites a cursor byte every field) */
        }
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
        if (has_fdc() && (a & 0xFFF0) == 0xFCC0) {
            elk_plus3_write(a, v);
        }
    }
    /* ROM: nothing */
}

/* ---- Set up and reset ---- */

bool elk_init(const elk_config_t *config)
{
    if (config->adfs && config->dfs) {
        return false;                 /* the two interfaces use the same registers */
    }
    if (config->dfs && !s_dfs_ram) {
        s_dfs_ram = ELK_SLOW_ALLOC(4096);
        if (!s_dfs_ram) {
            return false;
        }
    }
    elk.control = 0;
    elk_tape_control();
    memset(&elk, 0, sizeof(elk));
    elk.cfg = *config;
    elk.ram = config->ram;
    elk.basic = config->os_basic;
    elk.mos = config->os_basic + 0x4000;
    elk.adfs = config->adfs;
    memset(s_ff_page, 0xFF, sizeof(s_ff_page));
    if (!elk_video_init() || (has_fdc() && !elk_plus3_init())) {
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
    if (elk.cfg.dfs) {
        memset(s_dfs_ram, 0, 4096);
    }
    elk.frame_start = elk.cpu.cycles;
    elk.field_ticks = 312 * ELK_LINE_TICKS;
    elk_ula_reset(true);
    elk_plus3_reset();
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

/* ---- The field ---- */

/* The CPU up to tick t (held while BREAK is down; started again from its
 * reset vector as it comes up)
 */
static void run_to(uint64_t t)
{
    static bool was_down;
    if (elk.break_down) {
        if (elk.cpu.cycles < t) {
            elk.cpu.cycles = t;
        }
    } else {
        if (was_down) {
            elk_ula_reset(false);
            elk_plus3_reset();
            map_memory();
            m6502_reset(&elk.cpu);
        }
        while (elk.cpu.cycles < t) {
            if (elk_tape_event <= elk.cpu.cycles) {
                elk_tape_advance(elk.cpu.cycles);
            }
            elk.cpu.until = elk_tape_event < t ? elk_tape_event : t;
            uint64_t before = elk.cpu.cycles;
            m6502_run(&elk.cpu);
            if (elk.cpu.cycles == before) {
                break;
            }
        }
        if (elk_tape_event <= elk.cpu.cycles) {
            elk_tape_advance(elk.cpu.cycles);
        }
    }
    was_down = elk.break_down;
}

/* One field: 312 lines, then 313 (the 50 Hz interrupt 1 us before line 99's
 * first pixel in the first, 31 us after it in the second); display end at
 * the HS after the last picture line
 */
void elk_run_frame(void)
{
    bool long_field = elk.frames & 1;
    int lines = long_field ? 313 : 312;
    elk.field_ticks = lines * ELK_LINE_TICKS;
    elk_video_frame_start();
    for (elk.line = 0; elk.line < lines; elk.line++) {
        elk_video_line(elk.line);
        uint64_t start = elk_line_start();
        if (elk.line == ELK_RTC_LINE - 1 && !long_field) {
            run_to(start + ELK_LINE_TICKS - 2);
            elk_ula_raise(INT_RTC);
        } else if (elk.line == ELK_RTC_LINE && long_field) {
            run_to(start + 62);
            elk_ula_raise(INT_RTC);
        }
        if (elk.line == elk_display_lines() - 1) {
            run_to(start + ELK_DISPLAY_END_TICK);
            elk_ula_raise(INT_DISPLAY_END);
        }
        run_to(start + ELK_LINE_TICKS);
        elk_sound_line();
    }
    if (has_fdc()) {
        elk_plus3_update();
    }
    elk.frame_start += elk.field_ticks;
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
    s->rom_page = (elk.rom_page & 0x0C) == 8 ? elk.rom_page : elk.external_page;
    s->caps_lock = (elk.control & 0x80) != 0;
    s->motor = (elk.control & 0x40) != 0;
    s->comms = (elk.control >> 1) & 3;
    s->counter = elk.counter;
    s->screen_start = elk.screen_start;
    s->int_enable = elk.int_enable;
}
