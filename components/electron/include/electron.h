#pragma once

/*
 * Acorn Electron: a 6502A at 2 MHz (1 MHz to RAM and the ULA), 32 KB RAM,
 * the ULA (display, sound, cassette, keyboard, interrupts, ROM paging),
 * BBC BASIC II and the MOS in a 32 KB ROM; an Acorn Plus 3 (ADFS, a WD1770
 * and a 3.5" drive) as an option. Platform independent: the board's main.c
 * and the host build (host/) drive it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ELK_OS_BASIC_SIZE       (32 * 1024)     /* BASIC $8000-$BFFF, then the MOS */
#define ELK_PAGED_ROM_SIZE      (16 * 1024)
#define ELK_RAM_SIZE            (32 * 1024)
#define ELK_PLUS1_ROM_SIZE      (4 * 1024)
#define ELK_CART_SLOTS          2

/* The picture: 640 x 256 (modes with fewer pixels drawn wider) */
#define ELK_SCREEN_WIDTH        640
#define ELK_SCREEN_HEIGHT       256
#define ELK_SCREEN_HZ           50

/* Sound: 16-bit stereo */
#define ELK_AUDIO_HZ            31250

typedef struct {
    const uint8_t *os_basic;    /* ELK_OS_BASIC_SIZE bytes */
    uint8_t *ram;               /* ELK_RAM_SIZE bytes */
    /* The Plus 3's ADFS ROM (ELK_PAGED_ROM_SIZE bytes); NULL: no Plus 3 */
    const uint8_t *adfs;
    const uint8_t *plus1;       /* optional Expansion 1.00, ELK_PLUS1_ROM_SIZE bytes */
    const uint8_t *dfs;         /* ACP 1770 DFS in SK1, ELK_PAGED_ROM_SIZE bytes */
} elk_config_t;

bool elk_init(const elk_config_t *config);
void elk_reset(void);                   /* power off and on */
void elk_run_frame(void);
bool elk_halted(void);

/* Plus 1 sockets SK1 and SK2, in that order. Reset after changing media
 * so the MOS rescans the sideways ROMs.
 */
bool elk_cart_insert(int slot, const char *path);
void elk_cart_eject(int slot);
const char *elk_cart_path(int slot);
size_t elk_cart_size(int slot);

/* ---- Display ---- */
uint32_t elk_screen_signature(void);
void elk_render_line_rgb565(int y, uint16_t *dst);

/* ---- Keyboard: keys by matrix position (column * 4 + row) ----
 *
 * 14 columns of 4: a read of $8000-$BFFF with ROM page 8 or 9 returns in
 * bits 0-3 the keys down in the columns whose address line A0-A13 is low
 * (the layout as MAME's electron.cpp has it, BSD-3-Clause). The second
 * symbol on a key comes with SHIFT, the third with CTRL: RIGHT | \,
 * LEFT ^ ~, UP £ {, DOWN _ }, COPY [ ].
 */
enum {
    ELK_KEY_RIGHT, ELK_KEY_COPY, ELK_KEY_NONE_2, ELK_KEY_SPACE,
    ELK_KEY_LEFT, ELK_KEY_DOWN, ELK_KEY_RETURN, ELK_KEY_DELETE,
    ELK_KEY_MINUS, ELK_KEY_UP, ELK_KEY_COLON, ELK_KEY_NONE_11,
    ELK_KEY_0, ELK_KEY_P, ELK_KEY_SEMICOLON, ELK_KEY_SLASH,
    ELK_KEY_9, ELK_KEY_O, ELK_KEY_L, ELK_KEY_PERIOD,
    ELK_KEY_8, ELK_KEY_I, ELK_KEY_K, ELK_KEY_COMMA,
    ELK_KEY_7, ELK_KEY_U, ELK_KEY_J, ELK_KEY_M,
    ELK_KEY_6, ELK_KEY_Y, ELK_KEY_H, ELK_KEY_N,
    ELK_KEY_5, ELK_KEY_T, ELK_KEY_G, ELK_KEY_B,
    ELK_KEY_4, ELK_KEY_R, ELK_KEY_F, ELK_KEY_V,
    ELK_KEY_3, ELK_KEY_E, ELK_KEY_D, ELK_KEY_C,
    ELK_KEY_2, ELK_KEY_W, ELK_KEY_S, ELK_KEY_X,
    ELK_KEY_1, ELK_KEY_Q, ELK_KEY_A, ELK_KEY_Z,
    ELK_KEY_ESCAPE, ELK_KEY_FUNC, ELK_KEY_CTRL, ELK_KEY_SHIFT,
    ELK_KEYS
};
void elk_key(int key, bool down);       /* from any task */
void elk_break(bool down);              /* the BREAK key: the 6502's reset */
bool elk_type(const char *text);        /* \n is RETURN; false while still typing */

/* ---- Sound ---- */
void elk_audio_enable(bool on);
int elk_audio_render(int16_t *dst, int frames);
int elk_audio_pending(void);

/* ---- Cassette ---- */
/* Tape operations run with the emulator paused. Status is available while running. */
typedef struct {
    size_t size, position;              /* uncompressed UEF bytes */
    uint32_t received;
    bool playing;
} elk_tape_status_t;

bool elk_tape_insert(const char *path);
void elk_tape_eject(void);
void elk_tape_rewind(void);
void elk_tape_fast(bool fast);
void elk_tape_status(elk_tape_status_t *status);

/* ---- Plus 3 ---- */

#define ELK_DISK_DRIVES 2
#define ELK_ADFS_SMALL  (160u * 1024)
#define ELK_ADFS_MEDIUM (320u * 1024)
#define ELK_ADFS_LARGE  (640u * 1024)
#define ELK_DFS_40      (100u * 1024)
#define ELK_DFS_80      (200u * 1024)

typedef struct {
    size_t size;
    int tracks, sides, head;
    bool protect, write_error, motor, dfs;
    uint32_t reads, writes;
} elk_disk_status_t;

typedef struct {
    bool enabled;
    int drive, side;
    uint8_t track, sector, command, status;
} elk_plus3_status_t;

/* Mount, eject and flush with the core paused. Disk numbers start at zero. */
bool elk_disk_insert(int drive, const char *path);
void elk_disk_eject(int drive);
const char *elk_disk_path(int drive);
bool elk_disk_create(const char *path, uint32_t size);
bool elk_disks_dirty(void);
bool elk_disks_flush(void);
void elk_disk_status(int drive, elk_disk_status_t *status);
void elk_plus3_status(elk_plus3_status_t *status);

/* ---- Status ---- */
typedef struct {
    uint32_t frames;
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint8_t mode;               /* the display mode, 0-6 */
    uint8_t rom_page;           /* $FE05's ROM number */
    bool caps_lock;             /* the CAPS LK light */
    bool motor;                 /* the cassette motor */
    uint8_t comms;              /* $FE07 bits 2-1: 0 cassette in, 1 sound, 2 cassette out */
    uint8_t counter;            /* $FE06: the sound's divider */
    uint16_t screen_start;      /* $FE02/$FE03 */
    uint8_t int_enable;         /* $FE00's enables */
} elk_status_t;

void elk_get_status(elk_status_t *s);
