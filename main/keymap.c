/*
 * The board's keyboards (Bluetooth, USB, the Tab5 Keyboard: HID usages) to
 * the Electron's keys, by position. PROVISIONAL: the matrix layout comes
 * with docs/electron-hardware.md.
 */
#include "electron.h"
#include "keymap.h"

void keymap_hid_key(uint8_t usage, bool pressed)
{
    (void)usage;
    (void)pressed;
}
