/*
 * The board's keyboards (Bluetooth, USB, the Tab5 Keyboard: HID usages) to
 * the Electron's keys, by position: letters, digits and punctuation where a
 * PC keyboard has them (`, left of 1, is ESCAPE as Esc is; =, right of -,
 * is the Electron's LEFT key there; [ and ] its UP and DOWN; \ COPY), Tab
 * and Alt FUNC (left of Q), Ctrl and Shift themselves. Kept for their use:
 * the cursor keys the Electron's, Backspace and Delete DELETE, End and
 * Insert COPY, Caps Lock CAPS LK (SHIFT + FUNC on the Electron), F1-F10 the
 * function keys f1-f9 and f0 (FUNC + 1-9, 0), Pause and F12 BREAK, the
 * keypad its digits and signs.
 */
#include <string.h>
#include "electron.h"
#include "keymap.h"

#define NONE    0xFF
#define BREAK   0xFE                    /* not a matrix key: the 6502's reset */

typedef struct {
    uint8_t key, with;                  /* with: NONE or a key held with it */
} keymap_t;

#define K(k)        { (k), NONE }
#define SHIFT(k)    { (k), ELK_KEY_SHIFT }
#define FUNC(k)     { (k), ELK_KEY_FUNC }

static const keymap_t usage_map[0x66] = {
    [0x04] = K(ELK_KEY_A), [0x05] = K(ELK_KEY_B), [0x06] = K(ELK_KEY_C), [0x07] = K(ELK_KEY_D),
    [0x08] = K(ELK_KEY_E), [0x09] = K(ELK_KEY_F), [0x0A] = K(ELK_KEY_G), [0x0B] = K(ELK_KEY_H),
    [0x0C] = K(ELK_KEY_I), [0x0D] = K(ELK_KEY_J), [0x0E] = K(ELK_KEY_K), [0x0F] = K(ELK_KEY_L),
    [0x10] = K(ELK_KEY_M), [0x11] = K(ELK_KEY_N), [0x12] = K(ELK_KEY_O), [0x13] = K(ELK_KEY_P),
    [0x14] = K(ELK_KEY_Q), [0x15] = K(ELK_KEY_R), [0x16] = K(ELK_KEY_S), [0x17] = K(ELK_KEY_T),
    [0x18] = K(ELK_KEY_U), [0x19] = K(ELK_KEY_V), [0x1A] = K(ELK_KEY_W), [0x1B] = K(ELK_KEY_X),
    [0x1C] = K(ELK_KEY_Y), [0x1D] = K(ELK_KEY_Z),
    [0x1E] = K(ELK_KEY_1), [0x1F] = K(ELK_KEY_2), [0x20] = K(ELK_KEY_3), [0x21] = K(ELK_KEY_4),
    [0x22] = K(ELK_KEY_5), [0x23] = K(ELK_KEY_6), [0x24] = K(ELK_KEY_7), [0x25] = K(ELK_KEY_8),
    [0x26] = K(ELK_KEY_9), [0x27] = K(ELK_KEY_0),
    [0x28] = K(ELK_KEY_RETURN),
    [0x29] = K(ELK_KEY_ESCAPE),
    [0x2A] = K(ELK_KEY_DELETE),         /* Backspace */
    [0x2B] = K(ELK_KEY_FUNC),           /* Tab */
    [0x2C] = K(ELK_KEY_SPACE),
    [0x2D] = K(ELK_KEY_MINUS),
    [0x2E] = K(ELK_KEY_LEFT),           /* = */
    [0x2F] = K(ELK_KEY_UP),             /* [ */
    [0x30] = K(ELK_KEY_DOWN),           /* ] */
    [0x31] = K(ELK_KEY_COPY),           /* \ */
    [0x32] = K(ELK_KEY_COPY),           /* non-US # */
    [0x33] = K(ELK_KEY_SEMICOLON),
    [0x34] = K(ELK_KEY_COLON),          /* ' */
    [0x35] = K(ELK_KEY_ESCAPE),         /* ` */
    [0x36] = K(ELK_KEY_COMMA),
    [0x37] = K(ELK_KEY_PERIOD),
    [0x38] = K(ELK_KEY_SLASH),
    [0x39] = SHIFT(ELK_KEY_FUNC),       /* Caps Lock: CAPS LK */
    [0x3A] = FUNC(ELK_KEY_1), [0x3B] = FUNC(ELK_KEY_2), [0x3C] = FUNC(ELK_KEY_3), [0x3D] = FUNC(ELK_KEY_4),
    [0x3E] = FUNC(ELK_KEY_5), [0x3F] = FUNC(ELK_KEY_6), [0x40] = FUNC(ELK_KEY_7), [0x41] = FUNC(ELK_KEY_8),
    [0x42] = FUNC(ELK_KEY_9), [0x43] = FUNC(ELK_KEY_0),       /* F1-F10: f1-f9, f0 */
    [0x45] = { BREAK, NONE },           /* F12 */
    [0x48] = { BREAK, NONE },           /* Pause/Break */
    [0x49] = K(ELK_KEY_COPY),           /* Insert */
    [0x4C] = K(ELK_KEY_DELETE),         /* Delete */
    [0x4D] = K(ELK_KEY_COPY),           /* End */
    [0x4F] = K(ELK_KEY_RIGHT), [0x50] = K(ELK_KEY_LEFT), [0x51] = K(ELK_KEY_DOWN), [0x52] = K(ELK_KEY_UP),
    [0x54] = K(ELK_KEY_SLASH),          /* the keypad */
    [0x55] = SHIFT(ELK_KEY_COLON),      /* * */
    [0x56] = K(ELK_KEY_MINUS),
    [0x57] = SHIFT(ELK_KEY_SEMICOLON),  /* + */
    [0x58] = K(ELK_KEY_RETURN),
    [0x59] = K(ELK_KEY_1), [0x5A] = K(ELK_KEY_2), [0x5B] = K(ELK_KEY_3), [0x5C] = K(ELK_KEY_4),
    [0x5D] = K(ELK_KEY_5), [0x5E] = K(ELK_KEY_6), [0x5F] = K(ELK_KEY_7), [0x60] = K(ELK_KEY_8),
    [0x61] = K(ELK_KEY_9), [0x62] = K(ELK_KEY_0),
    [0x63] = K(ELK_KEY_PERIOD),
};

static uint8_t s_count[ELK_KEYS];       /* presses holding each key */
static keymap_t s_held[256];            /* per HID usage: what it pressed */

static void key_ref(uint8_t key, bool down)
{
    if (key == NONE) {
        return;
    }
    if (key == BREAK) {
        elk_break(down);
        return;
    }
    bool was = s_count[key] > 0;
    s_count[key] = down ? s_count[key] + 1 : s_count[key] ? s_count[key] - 1 : 0;
    bool now = s_count[key] > 0;
    if (was != now) {
        elk_key(key, now);
    }
}

void keymap_hid_key(uint8_t usage, bool pressed)
{
    if (usage >= 0xE0 && usage <= 0xE7) {
        /* LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI */
        static const uint8_t keys[8] = {
            ELK_KEY_CTRL, ELK_KEY_SHIFT, ELK_KEY_FUNC, NONE, ELK_KEY_CTRL, ELK_KEY_SHIFT, ELK_KEY_FUNC, NONE,
        };
        key_ref(keys[usage - 0xE0], pressed);
        return;
    }
    if (pressed) {
        if (usage >= sizeof(usage_map) / sizeof(usage_map[0])) {
            return;
        }
        keymap_t m = usage_map[usage];
        if (m.key == 0 && m.with == 0) {
            return;                     /* not an Electron key (RIGHT, key 0, has with NONE) */
        }
        s_held[usage] = m;
        key_ref(m.with, true);          /* the modifier first */
        key_ref(m.key, true);
    } else {
        keymap_t m = s_held[usage];
        if (m.key == 0 && m.with == 0) {
            return;
        }
        key_ref(m.key, false);
        key_ref(m.with, false);
        memset(&s_held[usage], 0, sizeof(s_held[usage]));
    }
}
