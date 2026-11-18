/*
 * Acorn Electron emulator on the Makerfabs MaTouch ESP32-S3 7" and the
 * M5Stack Tab5.
 *
 * The board side (tasks, LCD, touch, WiFi, Bluetooth, setup screen, web
 * page, console) is the shared runtime in ../esp32emu; this file plugs the
 * Electron into it: memory, ROMs, settings, keyboard map, status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "emu/emu.h"
#include "electron.h"
#include "keymap.h"
#include "rom_store.h"
#include "settings.h"

static const char *TAG = "main";

static elk_config_t s_cfg;
static uint8_t *s_os_basic, *s_adfs;

/* ---- Memory: everything at power-on, before the screen, WiFi and
 * Bluetooth fragment PSRAM
 */
static void reserve_memory(void)
{
    settings_load();
    s_os_basic = heap_caps_malloc(ELK_OS_BASIC_SIZE, MALLOC_CAP_SPIRAM);
    s_adfs = heap_caps_malloc(ELK_PAGED_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_cfg.ram = heap_caps_malloc(ELK_RAM_SIZE, MALLOC_CAP_SPIRAM);
}

static void storage(bool sd_card)
{
    emu_set_drive_info(sd_card ? "Tapes (.UEF) from the SD card: they don't play yet" : "No SD card: no tapes");
}

/* ---- The tape drive: UEF files in the SD card's electron/ folder, listed,
 * uploaded and downloaded by the web page. Tapes don't play yet (TODO.md 1):
 * the drive only holds the name, and says so
 */

static char s_tape[160];

/* A UEF file, or one gzip-compressed (the usual kind, same extension) */
static bool tape_insert(int drive, const char *path)
{
    uint8_t head[10];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(head, 1, sizeof(head), f) : 0;
    if (f) {
        fclose(f);
    }
    bool uef = n == sizeof(head) && memcmp(head, "UEF File!", sizeof(head)) == 0;
    bool gzip = n >= 2 && head[0] == 0x1f && head[1] == 0x8b;
    if (!uef && !gzip) {
        ESP_LOGW(TAG, "%s: not a UEF file", path);
        return false;
    }
    snprintf(s_tape, sizeof(s_tape), "%s", path);
    return true;
}

static void tape_eject(int drive)
{
    s_tape[0] = 0;
}

static const char *tape_path(int drive)
{
    return s_tape[0] ? s_tape : NULL;
}

static bool tape_write_protected(int drive)
{
    return false;
}

static const char *const extensions[] = { ".uef", NULL };
static const char *const drive_names[] = { "Tape" };

static const disks_config_t disks = {
    .folder = "electron",
    .extensions = extensions,
    .description = "Tapes (.UEF)",
    .title = "Tapes",
    .button = "Tapes",
    .nvs_namespace = "elkdisks",
    .drives = 1,
    .drive_names = drive_names,
    .max_upload = 4 * 1024 * 1024,
    .insert = tape_insert,
    .eject = tape_eject,
    .path = tape_path,
    .write_protected = tape_write_protected,
};

/* ---- Starting ---- */

static bool start(void)
{
    if (s_os_basic == NULL || s_adfs == NULL || s_cfg.ram == NULL) {
        emu_set_message("Not enough memory: see the log.");
        return false;
    }
    if (!rom_store_load(ROM_OS_BASIC, s_os_basic)) {
        static char message[160];
        snprintf(message, sizeof(message), "No ROM: put %s into roms/ and flash with idf.py flash.",
                 rom_store_name(ROM_OS_BASIC));
        emu_set_message(message);
        return false;
    }
    s_cfg.os_basic = s_os_basic;
    s_cfg.adfs = NULL;                  /* (the Plus 3 to come) */
    if (!elk_init(&s_cfg)) {
        emu_set_message("The Electron could not be set up: see the log.");
        return false;
    }
    ESP_LOGI(TAG, "Acorn Electron");
    return true;
}

static void screen_mode(emu_screen_t *mode)
{
    mode->width = ELK_SCREEN_WIDTH;
    mode->height = ELK_SCREEN_HEIGHT;
    mode->hz = ELK_SCREEN_HZ;
}

/* ---- Status ---- */

static void status(emu_json_t *j, int section)
{
    if (section == EMU_STATUS_DRIVES) {
        elk_status_t s;
        elk_get_status(&s);
        /* the light: the cassette motor's relay ($FE07 bit 6) */
        emu_json_drive(j, drive_names[0], disks_name(0), false, s.motor, 0, 0,
                       disks_name(0) ? "Tapes don't play yet (to come): LOAD and CHAIN won't find it" :
                       "Tapes don't play yet (to come)");
        return;
    }
    static const struct {
        const char *size;
        uint16_t base;
    } modes[7] = {
        { "640 × 256, 2 colours, 80 × 32 text", 0x3000 }, { "320 × 256, 4 colours, 40 × 32 text", 0x3000 },
        { "160 × 256, 16 colours, 20 × 32 text", 0x3000 }, { "640 × 200, 2 colours, 80 × 25 text", 0x4000 },
        { "320 × 256, 2 colours, 40 × 32 text", 0x5800 }, { "160 × 256, 4 colours, 20 × 32 text", 0x5800 },
        { "320 × 200, 2 colours, 40 × 25 text", 0x6000 },
    };
    char text[160];
    elk_status_t s;
    elk_get_status(&s);
    emu_json_row(j, "System", "Acorn Electron: a 6502A at 2 MHz (1 MHz to RAM), 32 KB RAM, the ULA");
    const char *page = s.rom_page == 8 || s.rom_page == 9 ? "the keyboard" :
                       s.rom_page == 10 || s.rom_page == 11 ? "BASIC" : NULL;
    if (page) {
        snprintf(text, sizeof(text), "MOS 1.00, BBC BASIC II; at &amp;8000 now: %s", page);
    } else {
        snprintf(text, sizeof(text), "MOS 1.00, BBC BASIC II; at &amp;8000 now: ROM %u (empty)", s.rom_page);
    }
    emu_json_row(j, "ROM", text);
    unsigned m = s.mode < 7 ? s.mode : 6;
    snprintf(text, sizeof(text), "Mode %u: %s; screen memory &amp;%04X-&amp;7FFF, starting at &amp;%04X%s", m, modes[m].size,
             modes[m].base, s.screen_start, m < 4 ? " (the CPU waits for RAM while a line is drawn)" : "");
    emu_json_row(j, "Screen", text);
    if (s.comms == 1) {
        snprintf(text, sizeof(text), "A tone of %lu Hz (&amp;FE06 = %u)", 1000000ul / (32ul * (s.counter + 1)), s.counter);
    } else {
        snprintf(text, sizeof(text), "Off%s", s.comms == 0 ? "" : s.comms == 2 ? " (cassette output)" : "");
    }
    emu_json_row(j, "Sound", text);
    snprintf(text, sizeof(text), "CAPS LK %s; by position, Tab and Alt FUNC, F1-F10 f1-f9 and f0, End COPY, "
             "Pause or F12 BREAK", s.caps_lock ? "on" : "off");
    emu_json_row(j, "Keyboard", text);
    emu_json_row(j, "Cassette", s.motor ? "Motor on; tapes don't play yet (to come)" :
                 "Motor off; tapes don't play yet (to come)");
    emu_json_row(j, "Expansions", "None (the Plus 3's floppy disks to come)");
}

/* ---- Console ---- */

static void console_info(char *buf, size_t len)
{
    elk_status_t s;
    elk_get_status(&s);
    snprintf(buf, len, "frames %lu PC %04X A %02X X %02X Y %02X S %02X P %02X mode %u ROM %u",
             (unsigned long)s.frames, s.pc, s.a, s.x, s.y, s.s, s.p, s.mode, s.rom_page);
}

/*   k <n>              tap matrix key n (column * 4 + row)
 *   K <n> <0|1>        key n down (1) or up (0)
 *   T <text>           type text (\n is RETURN)
 *   B                  BREAK
 */
static bool console_command(const char *line)
{
    switch (line[0]) {
    case 'k': {
        unsigned key = strtoul(line + 1, NULL, 10);
        elk_key(key, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        elk_key(key, false);
        return true;
    }
    case 'K': {
        unsigned key = 0, down = 0;
        if (sscanf(line + 1, "%u %u", &key, &down) == 2) {
            elk_key(key, down);
        }
        return true;
    }
    case 'T': {
        static char text[200];
        const char *s = line + 1;
        while (*s == ' ') {
            s++;
        }
        size_t n = 0;
        for (; *s && n < sizeof(text) - 1; s++) {
            if (s[0] == '\\' && s[1] == 'n') {
                text[n++] = '\n';
                s++;
            } else {
                text[n++] = *s;
            }
        }
        text[n] = 0;
        elk_type(text);
        return true;
    }
    case 'B':
        elk_break(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        elk_break(false);
        return true;
    default:
        return false;
    }
}

/* ---- The machine ---- */

static const emu_machine_t machine = {
    .name = "Acorn Electron",
    .short_name = "Electron",
    .view_width = 800,                  /* 640 x 256 on a 4:3 screen */
    .scale = EMU_SCALE_SMOOTH,
    .reserve_memory = reserve_memory,
    .storage = storage,
    .start = start,
    .reset = elk_reset,
    .run_frame = elk_run_frame,
    .halted = elk_halted,
    .screen_mode = screen_mode,
    .screen_signature = elk_screen_signature,
    .render_line = elk_render_line_rgb565,
    .audio_hz = ELK_AUDIO_HZ,
    .audio_enable = elk_audio_enable,
    .audio_render = elk_audio_render,
    .audio_pending = elk_audio_pending,
    .hid_key = keymap_hid_key,
    .disks = &disks,
    .status = status,
    .console_info = console_info,
    .console_command = console_command,
};

void app_main(void)
{
    emu_run(&machine);
}
