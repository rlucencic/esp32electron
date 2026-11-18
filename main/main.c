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
    emu_set_drive_info(sd_card ? "Tapes and disks on the SD card (to come)" : "No SD card");
}

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
    if (section != EMU_STATUS_ROWS) {
        return;
    }
    char text[96];
    elk_status_t s;
    elk_get_status(&s);
    emu_json_row(j, "System", "Acorn Electron, 32 KB");
    emu_json_row(j, "ROM", "MOS 1.00, BBC BASIC II");
    snprintf(text, sizeof(text), "Mode %u%s", s.mode, s.caps_lock ? ", CAPS LK" : "");
    emu_json_row(j, "Screen", text);
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
    .status = status,
    .console_info = console_info,
    .console_command = console_command,
};

void app_main(void)
{
    emu_run(&machine);
}
