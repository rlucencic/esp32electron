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
#include <strings.h>
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
static uint8_t *s_os_basic, *s_adfs, *s_plus1;

/* ---- Memory: everything at power-on, before the screen, WiFi and
 * Bluetooth fragment PSRAM
 */
static void reserve_memory(void)
{
    settings_load();
    s_os_basic = heap_caps_malloc(ELK_OS_BASIC_SIZE, MALLOC_CAP_SPIRAM);
    s_adfs = heap_caps_malloc(ELK_PAGED_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_plus1 = heap_caps_malloc(ELK_PLUS1_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_cfg.ram = heap_caps_malloc(ELK_RAM_SIZE, MALLOC_CAP_SPIRAM);
}

static void storage(bool sd_card)
{
    emu_set_drive_info(sd_card ? "Tapes, disks and ROM cartridges on the SD card" : "No SD card: no media");
}

/* ---- Tape and floppy media ---- */

enum { DRIVE_TAPE, DRIVE_0, DRIVE_1, DRIVE_SK1, DRIVE_SK2, DRIVE_COUNT };

static char s_tape[160];

static bool media_accepts(int drive, const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return false;
    }
    if (drive >= DRIVE_SK1) {
        return !strcasecmp(ext, ".rom") || !strcasecmp(ext, ".bin");
    }
    return drive == DRIVE_TAPE ? !strcasecmp(ext, ".uef") :
           !strcasecmp(ext, ".adf") || !strcasecmp(ext, ".ads") ||
           !strcasecmp(ext, ".adm") || !strcasecmp(ext, ".adl");
}

static bool media_insert(int drive, const char *path)
{
    if (!media_accepts(drive, path)) {
        return false;
    }
    if (drive >= DRIVE_SK1) {
        return elk_cart_insert(drive - DRIVE_SK1, path);
    }
    if (drive != DRIVE_TAPE) {
        return elk_disk_insert(drive - DRIVE_0, path);
    }
    if (strlen(path) >= sizeof(s_tape) || !elk_tape_insert(path)) {
        ESP_LOGW(TAG, "%s: invalid or unreadable UEF", path);
        return false;
    }
    snprintf(s_tape, sizeof(s_tape), "%s", path);
    return true;
}

static void media_eject(int drive)
{
    if (drive == DRIVE_TAPE) {
        elk_tape_eject();
        s_tape[0] = 0;
    } else if (drive >= DRIVE_SK1) {
        elk_cart_eject(drive - DRIVE_SK1);
    } else {
        elk_disk_eject(drive - DRIVE_0);
    }
}

static const char *media_path(int drive)
{
    if (drive == DRIVE_TAPE) {
        return s_tape[0] ? s_tape : NULL;
    }
    if (drive >= DRIVE_SK1) {
        return elk_cart_path(drive - DRIVE_SK1);
    }
    return elk_disk_path(drive - DRIVE_0);
}

static bool media_write_protected(int drive)
{
    if (drive == DRIVE_TAPE || drive >= DRIVE_SK1) {
        return true;
    }
    elk_disk_status_t disk;
    elk_disk_status(drive - DRIVE_0, &disk);
    return disk.protect;
}

static bool takes_blank(int drive)
{
    return drive == DRIVE_0 || drive == DRIVE_1;
}

static const char *new_extension(uint32_t size)
{
    return size == ELK_ADFS_LARGE ? ".adl" : ".adf";
}

static const char *disk_offer(int drive)
{
    if (takes_blank(drive) && !settings.plus3 && media_path(drive)) {
        return "Enable the Plus 3 and restart to use this ADFS disk.";
    }
    return NULL;
}

static void enable_plus3(int drive)
{
    settings.plus3 = 1;
    settings_save();
}

static const char *const extensions[] = { ".uef", ".adf", ".ads", ".adm", ".adl", ".rom", ".bin", NULL };
static const char *const drive_names[] = { "Tape", "Drive 0", "Drive 1", "Cartridge SK1", "Cartridge SK2" };
static const disks_size_t sizes[] = {
    { ELK_ADFS_MEDIUM, "320 KiB ADFS M (.ADF)" },
    { ELK_ADFS_SMALL, "160 KiB ADFS S (.ADF)" },
    { ELK_ADFS_LARGE, "640 KiB ADFS L (.ADL)" },
};

static const disks_config_t disks = {
    .folder = "electron",
    .extensions = extensions,
    .description = "UEF tapes, ADFS disks and ROM cartridges",
    .title = "Tapes, disks and cartridges",
    .button = "Media",
    .nvs_namespace = "elkdisks",
    .drives = DRIVE_COUNT,
    .drive_names = drive_names,
    .new_prefix = "ADFS",
    .new_extension = ".adf",
    .sizes = sizes,
    .size_count = sizeof(sizes) / sizeof(sizes[0]),
    .max_upload = 4 * 1024 * 1024,
    .insert = media_insert,
    .eject = media_eject,
    .path = media_path,
    .write_protected = media_write_protected,
    .create = elk_disk_create,
    .accepts = media_accepts,
    .takes_blank = takes_blank,
    .extension_for = new_extension,
    .offer = disk_offer,
    .take_offer = enable_plus3,
};

static void flush_disks(void *arg)
{
    elk_disks_flush();
}

static void every_second(void)
{
    if (elk_disks_dirty()) {
        emu_paused(flush_disks, NULL);
    }
}

/* ---- Starting ---- */

static bool start(void)
{
    if (s_os_basic == NULL || s_cfg.ram == NULL || (settings.plus3 && s_adfs == NULL)) {
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
    s_cfg.plus1 = s_plus1 && rom_store_load(ROM_PLUS1, s_plus1) ? s_plus1 : NULL;
    s_cfg.adfs = NULL;
    if (settings.plus3) {
        if (!rom_store_load(ROM_ADFS, s_adfs)) {
            emu_set_message("Plus 3 needs adfs.rom. Add the ROM or switch Plus 3 off.");
            return false;
        }
        s_cfg.adfs = s_adfs;
    }
    if (!elk_init(&s_cfg)) {
        emu_set_message("The Electron could not be set up: see the log.");
        return false;
    }
    elk_tape_fast(settings.fast_tape);
    ESP_LOGI(TAG, "Acorn Electron");
    return true;
}

static void screen_mode(emu_screen_t *mode)
{
    mode->width = ELK_SCREEN_WIDTH;
    mode->height = ELK_SCREEN_HEIGHT;
    mode->hz = ELK_SCREEN_HZ;
}

static const char *const tape_modes[] = { "Real time", "Fast leaders", "" };

static int tape_mode(void)
{
    return settings.fast_tape;
}

static void set_tape_mode(int mode)
{
    settings.fast_tape = mode != 0;
    settings_save();
    elk_tape_fast(settings.fast_tape);
}

static const char *const plus3_modes[] = { "Off", "On", "" };

static int plus3_mode(void)
{
    return settings.plus3;
}

static void set_plus3(int mode)
{
    settings.plus3 = mode != 0;
    settings_save();
}

static const emu_choice_t choices[] = {
    { "Plus 3", plus3_modes, plus3_mode, set_plus3, false },
    { "Tape speed", tape_modes, tape_mode, set_tape_mode, true },
};

/* ---- Status ---- */

static void status(emu_json_t *j, int section)
{
    elk_tape_status_t tape;
    elk_tape_status(&tape);
    if (section == EMU_STATUS_DRIVES) {
        elk_status_t s;
        elk_get_status(&s);
        char detail[80];
        snprintf(detail, sizeof(detail), "UEF position %zu of %zu bytes", tape.position, tape.size);
        emu_json_drive(j, drive_names[0], disks_name(0), true, s.motor, tape.received, 0,
                       tape.size ? detail : NULL);
        for (int drive = 0; drive < ELK_DISK_DRIVES; drive++) {
            elk_disk_status_t disk;
            elk_disk_status(drive, &disk);
            snprintf(detail, sizeof(detail), "%zu KiB, %d tracks, %d side%s%s", disk.size / 1024,
                     disk.tracks, disk.sides, disk.sides == 1 ? "" : "s",
                     disk.write_error ? "; write pending after error" : "");
            emu_json_drive(j, drive_names[drive + DRIVE_0], disks_name(drive + DRIVE_0),
                           disk.protect, disk.motor, disk.reads, disk.writes,
                           disk.size || disk.write_error ? detail : NULL);
        }
        for (int slot = 0; slot < ELK_CART_SLOTS; slot++) {
            int drive = DRIVE_SK1 + slot;
            size_t size = elk_cart_size(slot);
            snprintf(detail, sizeof(detail), "%zu KiB ROM", size / 1024);
            emu_json_drive(j, drive_names[drive], disks_name(drive), true, false, 0, 0,
                           size ? detail : NULL);
        }
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
    elk_plus3_status_t plus3;
    elk_plus3_status(&plus3);
    const char *page = s.rom_page == 8 || s.rom_page == 9 ? "the keyboard" :
                       s.rom_page == 10 || s.rom_page == 11 ? "BASIC" :
                       s.rom_page == 4 && plus3.enabled ? "ADFS" :
                       s.rom_page == 12 && s_cfg.plus1 ? "Expansion 1.00" : NULL;
    if (!page && s.rom_page < 4 &&
        elk_cart_size(1 - (s.rom_page >> 1)) > (size_t)(s.rom_page & 1) * ELK_PAGED_ROM_SIZE) {
        page = s.rom_page < 2 ? "Cartridge SK2" : "Cartridge SK1";
    }
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
    snprintf(text, sizeof(text), "Motor %s%s, %s", s.motor ? "on" : "off",
             tape.playing ? " (playing)" : "", settings.fast_tape ? "fast leaders" : "real time");
    emu_json_row(j, "Cassette", text);
    if (plus3.enabled) {
        snprintf(text, sizeof(text), "Acorn Plus 3, ADFS ROM in slot 4, WD1770 command &amp;%02X status &amp;%02X, "
                 "track %u sector %u side %d", plus3.command, plus3.status, plus3.track, plus3.sector, plus3.side);
        emu_json_row(j, "Expansions", text);
    } else {
        emu_json_row(j, "Expansions", "Plus 3 off");
    }
    if (s_cfg.plus1 || elk_cart_size(0) || elk_cart_size(1)) {
        emu_json_row(j, "Plus 1", s_cfg.plus1 ? "Expansion 1.00 and two cartridge slots" : "Two cartridge slots");
    }
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
static void rewind_tape(void *arg)
{
    elk_tape_rewind();
}

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
    case 'W':
        emu_paused(rewind_tape, NULL);
        return true;
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
    .choices = choices,
    .choice_count = sizeof(choices) / sizeof(choices[0]),
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
    .every_second = every_second,
    .console_info = console_info,
    .console_command = console_command,
};

void app_main(void)
{
    emu_run(&machine);
}
