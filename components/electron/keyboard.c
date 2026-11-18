/*
 * The keyboard: 14 columns of 4 keys (electron.h's ELK_KEY_*). With ROM
 * page 8 or 9 selected, a read of $8000-$BFFF returns in bits 0-3 the keys
 * down in every column whose address line (A0-A13) is low; bits 4-7 read 0.
 * BREAK isn't in the matrix: it resets the 6502 (elk.c).
 *
 * Keys come from other tasks (the board's keyboards: atomic bits) and from
 * typed text (the emulation task, a key every TYPE_FRAMES: held for
 * HOLD_FRAMES with its SHIFT or CTRL, then let go, so a repeated letter
 * counts twice). Letters follow CAPS LK ($FE07 bit 7, the MOS's light):
 * with it on a capital is the plain key, a small letter SHIFT and the key.
 */
#include <string.h>
#include "elk_internal.h"

#define TYPE_FRAMES     5
#define HOLD_FRAMES     3

enum { PLAIN, SHIFT, CTRL, LETTER };

typedef struct {
    uint8_t key, how;
} typed_t;

static uint8_t s_typed[14];             /* typed keys down, the emulation task's */
static char s_text[256];
static int s_pos, s_len, s_wait;

/* Every character the keys make, as the MOS reads them */
static bool for_char(char ch, typed_t *t)
{
    static const struct {
        char c;
        uint8_t key, how;
    } table[] = {
        { ' ', ELK_KEY_SPACE, PLAIN }, { '\n', ELK_KEY_RETURN, PLAIN },
        { '0', ELK_KEY_0, PLAIN }, { '1', ELK_KEY_1, PLAIN }, { '2', ELK_KEY_2, PLAIN }, { '3', ELK_KEY_3, PLAIN },
        { '4', ELK_KEY_4, PLAIN }, { '5', ELK_KEY_5, PLAIN }, { '6', ELK_KEY_6, PLAIN }, { '7', ELK_KEY_7, PLAIN },
        { '8', ELK_KEY_8, PLAIN }, { '9', ELK_KEY_9, PLAIN },
        { '@', ELK_KEY_0, SHIFT }, { '!', ELK_KEY_1, SHIFT }, { '"', ELK_KEY_2, SHIFT }, { '#', ELK_KEY_3, SHIFT },
        { '$', ELK_KEY_4, SHIFT }, { '%', ELK_KEY_5, SHIFT }, { '&', ELK_KEY_6, SHIFT }, { '\'', ELK_KEY_7, SHIFT },
        { '(', ELK_KEY_8, SHIFT }, { ')', ELK_KEY_9, SHIFT },
        { '-', ELK_KEY_MINUS, PLAIN }, { '=', ELK_KEY_MINUS, SHIFT },
        { ';', ELK_KEY_SEMICOLON, PLAIN }, { '+', ELK_KEY_SEMICOLON, SHIFT },
        { ':', ELK_KEY_COLON, PLAIN }, { '*', ELK_KEY_COLON, SHIFT },
        { ',', ELK_KEY_COMMA, PLAIN }, { '<', ELK_KEY_COMMA, SHIFT },
        { '.', ELK_KEY_PERIOD, PLAIN }, { '>', ELK_KEY_PERIOD, SHIFT },
        { '/', ELK_KEY_SLASH, PLAIN }, { '?', ELK_KEY_SLASH, SHIFT },
        { '^', ELK_KEY_LEFT, SHIFT }, { '~', ELK_KEY_LEFT, CTRL },
        { '|', ELK_KEY_RIGHT, SHIFT }, { '\\', ELK_KEY_RIGHT, CTRL },
        { '_', ELK_KEY_DOWN, SHIFT }, { '}', ELK_KEY_DOWN, CTRL },
        { '{', ELK_KEY_UP, CTRL },
        { '[', ELK_KEY_COPY, SHIFT }, { ']', ELK_KEY_COPY, CTRL },
    };
    static const uint8_t letters[26] = {
        ELK_KEY_A, ELK_KEY_B, ELK_KEY_C, ELK_KEY_D, ELK_KEY_E, ELK_KEY_F, ELK_KEY_G, ELK_KEY_H, ELK_KEY_I,
        ELK_KEY_J, ELK_KEY_K, ELK_KEY_L, ELK_KEY_M, ELK_KEY_N, ELK_KEY_O, ELK_KEY_P, ELK_KEY_Q, ELK_KEY_R,
        ELK_KEY_S, ELK_KEY_T, ELK_KEY_U, ELK_KEY_V, ELK_KEY_W, ELK_KEY_X, ELK_KEY_Y, ELK_KEY_Z,
    };
    if (ch >= 'A' && ch <= 'Z') {
        t->key = letters[ch - 'A'];
        t->how = (elk.control & 0x80) ? PLAIN : SHIFT;
        return true;
    }
    if (ch >= 'a' && ch <= 'z') {
        t->key = letters[ch - 'a'];
        t->how = (elk.control & 0x80) ? SHIFT : PLAIN;
        return true;
    }
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].c == ch) {
            t->key = table[i].key;
            t->how = table[i].how;
            return true;
        }
    }
    return false;
}

static inline void set_bit(uint8_t *m, int key, bool down)
{
    if (down) {
        m[key / 4] |= 1 << (key % 4);
    } else {
        m[key / 4] &= ~(1 << (key % 4));
    }
}

uint8_t elk_keyboard_read(uint16_t a)
{
    uint8_t v = 0;
    for (int col = 0; col < 14; col++) {
        if (!(a & (1 << col))) {
            v |= elk.matrix[col] | s_typed[col];
        }
    }
    return v & 0x0F;
}

void elk_key(int key, bool down)
{
    if (key < 0 || key >= ELK_KEYS) {
        return;
    }
    uint8_t bit = 1 << (key % 4);
    if (down) {
        __atomic_fetch_or(&elk.matrix[key / 4], bit, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_and(&elk.matrix[key / 4], (uint8_t)~bit, __ATOMIC_RELAXED);
    }
}

bool elk_type(const char *text)
{
    if (s_pos < s_len) {
        return false;                   /* still typing the last one */
    }
    size_t n = strlen(text);
    if (n >= sizeof(s_text)) {
        n = sizeof(s_text) - 1;
    }
    s_len = 0;
    memcpy(s_text, text, n);
    s_pos = 0;
    s_wait = 0;
    s_len = n;
    return true;
}

/* Once a frame, in the emulation task */
void elk_keyboard_frame(void)
{
    if (s_pos >= s_len) {
        return;
    }
    if (s_wait == HOLD_FRAMES) {
        memset(s_typed, 0, sizeof(s_typed));        /* let go */
    }
    if (s_wait > 0) {
        s_wait--;
        if (s_wait == 0) {
            s_pos++;
        }
        return;
    }
    typed_t t;
    while (s_pos < s_len && !for_char(s_text[s_pos], &t)) {
        s_pos++;                        /* nothing types it */
    }
    if (s_pos >= s_len) {
        return;
    }
    set_bit(s_typed, t.key, true);
    if (t.how == SHIFT) {
        set_bit(s_typed, ELK_KEY_SHIFT, true);
    } else if (t.how == CTRL) {
        set_bit(s_typed, ELK_KEY_CTRL, true);
    }
    s_wait = TYPE_FRAMES;
}
