/*
 * The keyboard: 14 columns of 4 keys. With ROM page 8 or 9 selected, a
 * read of $8000-$BFFF returns in bits 0-3 the keys down in every column
 * whose address line (A0-A13) is low. PROVISIONAL: the layout of keys in
 * the matrix, and typed text, come with docs/electron-hardware.md.
 */
#include <string.h>
#include "elk_internal.h"

uint8_t elk_keyboard_read(uint16_t a)
{
    uint8_t v = 0;
    for (int col = 0; col < 14; col++) {
        if (!(a & (1 << col))) {
            v |= elk.matrix[col];
        }
    }
    return v & 0x0F;
}

void elk_key(int key, bool down)
{
    if (key < 0 || key >= ELK_KEYS) {
        return;
    }
    int col = key / 4, bit = 1 << (key % 4);
    if (down) {
        elk.matrix[col] |= bit;
    } else {
        elk.matrix[col] &= ~bit;
    }
}

bool elk_type(const char *text)
{
    (void)text;
    return false;                       /* (with the key layout) */
}

void elk_keyboard_frame(void)
{
}
