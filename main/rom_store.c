#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "emu/sdcard.h"
#include "rom_store.h"

#define ROM_PARTITION_TYPE  0x40
#define ROM_FOLDER          SDCARD_MOUNT_POINT "/electron/roms"
#define SLOT                65536

static const char *TAG = "rom";

static const struct {
    const char *name;
    uint32_t size;
} roms[ROM_COUNT] = {
    [ROM_OS_BASIC] = { "os_basic.ic2", 32 * 1024 },
    [ROM_ADFS] = { "adfs.rom", 16 * 1024 },
    [ROM_PLUS1] = { "plus1.rom", 4 * 1024 },
};

const char *rom_store_name(rom_id_t id)
{
    return id < ROM_COUNT ? roms[id].name : "";
}

uint32_t rom_store_size(rom_id_t id)
{
    return id < ROM_COUNT ? roms[id].size : 0;
}

/* A paged ROM's header: the type byte at 6 says a service entry (bit 7)
 * or a language (bit 6: BASIC has no service entry), the byte at 7 points
 * at its "\0(C)"
 */
static bool paged_rom(const uint8_t *p)
{
    uint8_t c = p[7];
    return (p[6] & 0xC0) && c < 0xF0 && p[c] == 0 && p[c + 1] == '(' && p[c + 2] == 'C' && p[c + 3] == ')';
}

/* os_basic.ic2: BASIC's header in the first half, the MOS's reset vector
 * ($FFFC) inside the MOS in the second
 */
static bool plausible(rom_id_t id, const uint8_t *p)
{
    if (id != ROM_OS_BASIC) {
        return paged_rom(p);
    }
    uint16_t reset = p[0x7FFC] | (p[0x7FFD] << 8);
    return paged_rom(p) && reset >= 0xC000 && reset < 0xFC00;
}

/* Through a mapping, not esp_partition_read(): that turns the caches off
 * while it reads, which stalls the LCD's refill (picture artefacts)
 */
static bool from_partition(rom_id_t id, uint8_t *buf)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ROM_PARTITION_TYPE,
                                                           "elk_roms");
    uint32_t offset = id * SLOT, size = roms[id].size;
    if (part == NULL || offset + size > part->size) {
        return false;
    }
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, offset, size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        return false;
    }
    memcpy(buf, map, size);
    esp_partition_munmap(handle);
    return true;
}

/* electron/roms/<name> on the SD card (any case) */
static bool from_sd(rom_id_t id, uint8_t *buf)
{
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", ROM_FOLDER, roms[id].name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        for (char *p = path + strlen(ROM_FOLDER) + 1; *p; p++) {
            *p = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        }
        f = fopen(path, "rb");
    }
    if (f == NULL) {
        return false;
    }
    size_t n = fread(buf, 1, roms[id].size, f);
    fclose(f);
    if (n != roms[id].size) {
        return false;
    }
    ESP_LOGI(TAG, "%s from the SD card", roms[id].name);
    return true;
}

bool rom_store_load(rom_id_t id, uint8_t *buf)
{
    if (id >= ROM_COUNT) {
        return false;
    }
    if (from_sd(id, buf) && plausible(id, buf)) {
        return true;                    /* the card's overrides the built-in one */
    }
    return from_partition(id, buf) && plausible(id, buf);
}
