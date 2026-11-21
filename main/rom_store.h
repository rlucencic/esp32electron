#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The Electron's ROMs: built in, from the elk_roms flash partition
 * (roms/<name> in the project, written by idf.py flash into 64 KB slots).
 * A file of the same name in the SD card's "electron/roms" folder
 * overrides one.
 */
typedef enum { ROM_OS_BASIC, ROM_ADFS, ROM_PLUS1, ROM_COUNT } rom_id_t;

const char *rom_store_name(rom_id_t id);    /* "os_basic.ic2" */
uint32_t rom_store_size(rom_id_t id);       /* 32 KB, ADFS's 16 */

/* The ROM into buf (rom_store_size() bytes); false if there's none */
bool rom_store_load(rom_id_t id, uint8_t *buf);
