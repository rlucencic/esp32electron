#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elk_internal.h"

typedef struct {
    uint8_t *rom;
    size_t size;
    char path[192];
} cartridge_t;

static cartridge_t cartridges[ELK_CART_SLOTS];

static const char low_tag[] = "[lorom]";
static const char high_tag[] = "[uprom]";

const uint8_t *elk_cart_bank(unsigned page)
{
    /* SK1 is nearest the keyboard and has the higher priority banks. */
    const cartridge_t *cart = &cartridges[1 - (page >> 1)];
    size_t offset = (page & 1) * ELK_PAGED_ROM_SIZE;
    return offset < cart->size ? cart->rom + offset : NULL;
}

static bool read_other_bank(const char *path, const char *tag, const char *other_tag, uint8_t *bank)
{
    char other_path[sizeof(cartridges[0].path)];
    strcpy(other_path, path);
    memcpy(other_path + (tag - path), other_tag, sizeof(low_tag) - 1);
    FILE *file = fopen(other_path, "rb");
    if (!file) {
        return false;
    }
    bool ok = fread(bank, 1, ELK_PAGED_ROM_SIZE, file) == ELK_PAGED_ROM_SIZE &&
              fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok;
}

bool elk_cart_insert(int slot, const char *path)
{
    if (slot < 0 || slot >= ELK_CART_SLOTS || strlen(path) >= sizeof(cartridges[0].path)) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *tag = strstr(name, low_tag);
    bool upper = tag == NULL;
    if (upper) {
        tag = strstr(name, high_tag);
    }

    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    bool valid = tag ? size == ELK_PAGED_ROM_SIZE :
                       size == ELK_PAGED_ROM_SIZE / 2 || size == ELK_PAGED_ROM_SIZE ||
                       size == 2 * ELK_PAGED_ROM_SIZE;
    size_t capacity = tag || size == 2 * ELK_PAGED_ROM_SIZE ? 2 * ELK_PAGED_ROM_SIZE : ELK_PAGED_ROM_SIZE;
    size_t offset = tag && upper ? ELK_PAGED_ROM_SIZE : 0;
    uint8_t *rom = valid ? ELK_SLOW_ALLOC(capacity) : NULL;
    bool ok = rom && fseek(file, 0, SEEK_SET) == 0 && fread(rom + offset, 1, size, file) == (size_t)size;
    fclose(file);
    if (ok && tag) {
        ok = read_other_bank(path, tag, upper ? low_tag : high_tag, rom + ELK_PAGED_ROM_SIZE - offset);
    }
    if (!ok) {
        free(rom);
        return false;
    }
    if (size == ELK_PAGED_ROM_SIZE / 2) {
        memcpy(rom + size, rom, size);
    }
    cartridge_t *cart = &cartridges[slot];
    free(cart->rom);
    cart->rom = rom;
    cart->size = tag ? capacity : (size_t)size;
    strcpy(cart->path, path);
    if (elk.ram) {
        elk_map_rom();
    }
    return true;
}

void elk_cart_eject(int slot)
{
    if (slot < 0 || slot >= ELK_CART_SLOTS) {
        return;
    }
    free(cartridges[slot].rom);
    cartridges[slot] = (cartridge_t) { 0 };
    if (elk.ram) {
        elk_map_rom();
    }
}

const char *elk_cart_path(int slot)
{
    return elk_cart_size(slot) ? cartridges[slot].path : NULL;
}

size_t elk_cart_size(int slot)
{
    return slot >= 0 && slot < ELK_CART_SLOTS ? cartridges[slot].size : 0;
}
