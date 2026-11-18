/*
 * The ULA's registers, $FE00-$FE0F (repeated through $FE00-$FEFF):
 *
 *   $FE00  read: interrupt status (bit 0 any enabled one, 1 power-on reset,
 *          2 display end, 3 the 50 Hz clock, 4 receive full, 5 transmit
 *          empty, 6 high tone); write: their enables (bits 2-6)
 *   $FE02  write: screen start, address bits 6-8 from bits 5-7
 *   $FE03  write: screen start, address bits 9-14 from bits 0-5
 *   $FE04  the cassette's data
 *   $FE05  write: bits 0-3 the ROM page at $8000-$BFFF (from the keyboard
 *          and BASIC, pages 8-11, only another of 8-15 can be selected:
 *          the MOS selects 12 first); bits 4-7 clear display end, the
 *          clock, high tone, NMI
 *   $FE06  write: the counter (sound: 1 MHz / 16 / (n + 1); the cassette)
 *   $FE07  write: bits 1-2 00 cassette in, 01 sound, 10 cassette out;
 *          bits 3-5 the display mode; bit 6 the cassette motor; bit 7 the
 *          CAPS LK light
 *   $FE08-$FE0F  write: the palette (video.c)
 *
 * (docs/electron-hardware.md has the sources and what is still open.)
 */
#include <string.h>
#include "elk_internal.h"

#define RTC_LINE        99              /* the 50 Hz interrupt, part way down the picture */

void elk_ula_update_irq(void)
{
    bool any = (elk.int_status & elk.int_enable & 0x7C) != 0;
    elk.int_status = any ? elk.int_status | INT_MASTER : elk.int_status & ~INT_MASTER;
    elk.cpu.irq = any ? 1 : 0;
}

void elk_ula_reset(bool power_on)
{
    elk.int_status = power_on ? INT_POWER_ON : (elk.int_status & INT_POWER_ON);
    elk.int_enable = 0;
    elk.rom_page = 10;                  /* BASIC (the MOS selects it anyway) */
    elk.control = 0;
    elk.mode = 0;
    elk.counter = 0;
    if (power_on) {
        elk.screen_start = 0x3000;
        memset(elk.palette, 0, sizeof(elk.palette));
    }
    elk_ula_update_irq();
}

bool elk_contended_mode(void)
{
    return elk.mode < 4;
}

int elk_display_lines(void)
{
    return (elk.mode == 3 || elk.mode == 6) ? 250 : 256;
}

uint8_t elk_ula_read(uint16_t a)
{
    switch (a & 0x0F) {
    case 0x0: {
        uint8_t v = elk.int_status | 0x80;
        elk.int_status &= ~INT_POWER_ON;    /* read once */
        elk_ula_update_irq();
        return v;
    }
    case 0x4:
        elk.int_status &= ~INT_RX_FULL;
        elk_ula_update_irq();
        return 0;
    default:
        return 0;
    }
}

void elk_ula_write(uint16_t a, uint8_t v)
{
    switch (a & 0x0F) {
    case 0x0:
        elk.int_enable = v;
        elk_ula_update_irq();
        break;
    case 0x2:
        elk.screen_start = (elk.screen_start & 0x7E00) | ((v & 0xE0) << 1);
        break;
    case 0x3:
        elk.screen_start = ((v & 0x3F) << 9) | (elk.screen_start & 0x01C0);
        break;
    case 0x4:
        elk.int_status &= ~INT_TX_EMPTY;
        elk_ula_update_irq();
        break;
    case 0x5:
        if ((elk.rom_page & 0x0C) != 0x08 || (v & 0x08)) {
            elk.rom_page = v & 0x0F;
            elk_map_rom();
        }
        if (v & 0x10) {
            elk.int_status &= ~INT_DISPLAY_END;
        }
        if (v & 0x20) {
            elk.int_status &= ~INT_RTC;
        }
        if (v & 0x40) {
            elk.int_status &= ~INT_HIGH_TONE;
        }
        elk_ula_update_irq();
        break;
    case 0x6:
        elk.counter = v;
        break;
    case 0x7: {
        elk.control = v;
        int mode = (v >> 3) & 7;
        elk.mode = mode == 7 ? 6 : mode;
        break;
    }
    default:
        if ((a & 0x0F) >= 0x8) {
            elk.palette[(a & 0x0F) - 8] = v;
        }
        break;
    }
}

/* At each line's start: the frame's interrupts */
void elk_ula_line(void)
{
    if (elk.line == elk_display_lines()) {
        elk.int_status |= INT_DISPLAY_END;
        elk_ula_update_irq();
    } else if (elk.line == RTC_LINE) {
        elk.int_status |= INT_RTC;
        elk_ula_update_irq();
    }
}
