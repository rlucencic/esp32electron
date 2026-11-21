# Acorn Electron hardware, for a clean-room emulator core

Research notes, 2026-11-18. Every fact carries a source tag (key in
[Sources](#sources)). Marks used:

- **[confirmed]**: two or more independent sources agree.
- **[one source]**: a single source, not contradicted.
- **[derived]**: worked out here from ROM code, a schematic or numbers,
  not stated anywhere.
- **[UNCERTAIN]**: sources disagree, or nothing settles it; what each
  says is given.

Licences: MAME's Electron files (`electron.cpp`, `electron_ula.cpp/.h`,
`bus/electron/*`, `formats/acorn_dsk.cpp`, `formats/uef_cas.cpp`) carry
`// license:BSD-3-Clause` and are cited freely. Clock Signal (CLK) is MIT.
hoglet67's ElectronFpga (FPGA) is **GPL-3.0**: read only to confirm
behaviour, nothing quoted from it. Elkulator, B-em and ElectrEm source
code were not read; Elkulator/ElkJS/ElectrEm behaviour appears only as
reported in the Stardot thread. Independence caveats: TECH and CLK have
the same author (Thomas Harte); CLK's video timing is transcribed from
FPGA; MAME's ULA timing comment quotes TECH; the 3rd-5th editions of the
Electron AUG (AUG5) carry stardot members' corrections, some from the same
measurements that tuned FPGA. "Confirmed" below always includes at least
one source outside such a lineage (SM, the MOS/ADFS code, MAME's code,
the schematics) or two separate measurements.

### Summary

Certain: the memory map ($0000-$7FFF RAM, $8000-$BFFF paged, MOS
$C000-$FBFF and $FF00-$FFFF, I/O $FC00-$FEFF with the ULA in every 16
bytes of $FExx); ROM pages 8/9 = keyboard, 10/11 = BASIC, the rest
external; the ULA's $FE05 paging rule (while 8-11 is selected only writes
with bit 3 set are taken; external latches take only writes with the top
nibble 0); every ULA register bit (FE00, FE02/03, FE04, FE05, FE06, FE07,
FE08-FE0F palette layout); the interrupt sources and their clearing; the
keyboard matrix (A0-A13 select columns, D0-D3 rows, 1 = pressed); mode
geometry, screen bases and wrap sizes; 64 us lines of 128 2 MHz cycles,
40 us active; ROM at 2 MHz, RAM always at 1 MHz (1 us, or 1.5 us after a
2 MHz cycle), I/O at 1 MHz; modes 0-3 hold the CPU off RAM for the 40 us
active part of each pixel line (256 lines; 200 in mode 3), never on
other lines; the measured BASIC loop times per mode; interlaced 312.5-line
fields (50 Hz); display end at the start of HS after line 255 (249 in
modes 3/6), RTC at line 99; the sound formula 1 MHz / (32 (S+1)); the
cassette encoding (1200 baud, 0 = one 1200 Hz cycle, 1 = two 2400 Hz
cycles, start 0, 8 data LSB first, stop 1) and the MOS block format and
CRC; the UEF chunks; the Plus 3: WD1770 at $FCC4-$FCC7, control latch at
$FCC0 (bits 0/1 drive, 2 side, 3 single density, 5 not-reset), no
INTRQ/DRQ wiring, ADFS polls with IRQs off, ADFS in page 4; power-on
detected by FE00 bit 1, CTRL-BREAK by the MOS reading CTRL.

Uncertain (details in the text): what unmapped reads return (FC/FD
pages, empty ROM pages, write-only SHEILA registers); whether keyboard
reads run at 1 or 2 MHz; the contention window's exact edges and the
1 MHz slot phase (to one 2 MHz cycle); the keyboard read's high nibble;
whether FE07 mode changes act at once or at the line's end; when RxFull
clears and the exact high-tone rule; whether BREAK resets the ULA's
registers; a write of $FE06 restarting the tone or not; what reading
$FCC0 returns and whether $FCC8-$FCCF mirror the Plus 3; the side order
inside 2-sided ADFS image files; why a Plus 1 slows the BASIC loop. Single
sources: the RTC's position within line 99 (AUG5's scope pictures), the
NMI latch cleared by $FE05 bit 7, Plus 3 latch bit 4 driving NMI through
link LK1, the Plus 1 register bits. Places where one emulator differs
from the others are flagged "[CLK differs]" (paging on interrupt-clear
writes, FC/FD speed, RAM speed outside the window, UEF &0110/&0112
units).

Contents

1. [Memory map](#1-memory-map)
2. [The ULA's registers and interrupts](#2-the-ulas-registers-and-interrupts)
3. [Display](#3-display)
4. [CPU timing and contention](#4-cpu-timing-and-contention)
5. [Sound](#5-sound)
6. [Cassette and UEF](#6-cassette-and-uef)
7. [Keyboard](#7-keyboard)
8. [The Plus 1](#8-the-plus-1)
9. [The Plus 3](#9-the-plus-3)
10. [Reset and start-up](#10-reset-and-start-up)
11. [Sources](#sources)

---

## 1. Memory map

The CPU is a 6502A (Synertek SY6502A) clocked from the ULA's PHI OUT,
derived from a 16 MHz crystal [SM 5.1, SM parts list "6502A", TECH "The
CPU"] [confirmed]. The ULA (Ferranti 12C021) decodes everything on the
board; RAM is reached only through it [AUG5 p211, SM 6] [confirmed].

| Range | Contents | Speed (section 4) |
|---|---|---|
| $0000-$7FFF | 32K RAM (four 4164 DRAMs, 64 Kbit each, on a 4-bit bus: two accesses a byte, SM 5.1) | 1 MHz; stalled in modes 0-3 |
| $8000-$BFFF | paged: keyboard (pages 8, 9), BASIC (10, 11), external (0-7, 12-15) | ROM 2 MHz; keyboard [UNCERTAIN] |
| $C000-$FBFF | MOS (upper 16K of the 32K ROM) | 2 MHz |
| $FC00-$FCFF | FRED: expansion bus I/O (1 MHz bus) | 1 MHz |
| $FD00-$FDFF | JIM: expansion bus, paged by a register at $FCFF | 1 MHz |
| $FE00-$FEFF | SHEILA: the ULA, 16 registers repeated in every 16 bytes | 1 MHz |
| $FF00-$FFFF | MOS (vectors at $FFFA: NMI $0D00, RESET $D8D2, IRQ $DAE7) | 2 MHz |

Sources: [AUG5 pp204-205, p210, pp236-237; SM 5.1; FPGA `ROM_n_int`
(ROM selected for $C000-$FBFF, $FF00-$FFFF and pages 10/11); MAME
`electron_ula_device::device_reset`, `rom_r`, `io_r`] [confirmed]. Vector
values from the ROM [derived]. The MOS ROM holds a credits text at
$FC00-$FEFF that the I/O pages hide [AUG5 p205; os_basic.ic2 offsets
$7C00-$7EFF] [confirmed].

### 1.1 Paged ROM area and $FE05

The ULA has its own 4-bit page latch, written by $FE05 bits 3-0
[AUG5 pp219-220]:

| Page | What drives $8000-$BFFF | Source |
|---|---|---|
| 8, 9 | the ULA: keyboard (section 7) | AUG5 p220, p225; MAME `rom_r`; FPGA `kbd_access` [confirmed] |
| 10, 11 | the internal ROM's lower 16K (BBC BASIC II), the same in both | AUG5 p220; MAME; CLK; FPGA [confirmed] |
| 0-7, 12-15 | nothing on board; the expansion bus | AUG5 p220, p234; FPGA `data_en` [confirmed] |

**The ULA's write rule** [AUG5 p220 figure 14.4 and text; TECH "FE?5";
FPGA; MAME `io_w` case 0x05] [confirmed]: if the latch holds 8-11 (bit 3
set, bit 2 clear), a write is taken only if its bit 3 is set (8-15);
otherwise (latch 0-7 or 12-15) any value is taken. So BASIC stays paged
when interrupts are cleared with $10, $20, $40 and the MOS reaches an
external ROM by writing 12-15 first, then the number. The ULA applies the
rule to every $FE05 write, interrupt-clearing ones included [AUG5 p220
"writes to clear interrupts will also affect the lower bits"; FPGA;
MAME]; CLK instead ignores the page bits when any of bits 7-4 is set
(CLK `Electron.cpp` case 0xfe05) [CLK differs].

**External latches** (Plus 1, Plus 3, cartridges) latch $FE05 writes only
when bits 7-4 are 0 [AUG5 p234-235 "External devices are expected to
reject any paging requests where the high nibble is non-zero"; MAME
`plus1.cpp`, `plus3.cpp` `expbus_w`; Plus 3 schematic: a 74LS260 NOR on
DB4-DB7 gates the latch, P3SCH] [confirmed].

**MOS paging** [derived, MOS $E39F-$E3B0]: `select(n)`: store n at $F4,
write $0C to $FE05, then write n. The ROM number is kept at $F4 [AUG5
p234].

**Allocation** [AUG5 p236]: 0, 1 Plus 1 socket SK2; 2, 3 socket SK1;
4 disc (the Plus 3's ADFS); 5, 6 user; 7 modem; 8, 9 keyboard; 10, 11
BASIC; 12 Plus 1 OS; 13 high-priority expansion slot; 14 Econet; 15
reserved. MAME's comment table is the same [MAME `electron.cpp`
`rom_r`] [confirmed].

### 1.2 I/O pages

- **SHEILA $FE00-$FEFF**: only the ULA; A7-A4 are not decoded, so $FExN
  is register N [AUG5 p210 "the ULA appears in every 16 byte block";
  TECH; FPGA decodes `addr(3 downto 0)`; MAME `offset & 0x0f`; CLK
  `address & 0xff0f`] [confirmed]. External devices also see $FE05 writes
  (1.1).
- **FRED $FC00-$FCFF**: expansion only; Acorn's allocation [AUG5 p237]:
  $FC00-0F test, $FC10-13 Teletext, $FC14-1F Prestel, $FC20-27 IEEE488,
  $FC28-2F Econet, $FC30-3F Cambridge Ring, $FC40-47 hard discs,
  $FC60-6F 6850 ACIA, $FC70-7F Acorn expansions (Plus 1 $FC70-73),
  $FC90-9F sound/speech, $FCB0-BF 6522 VIA, $FCC0-CF floppy discs (Plus 3
  $FCC0-C7), $FCE0-EF Tube, $FCFF the JIM paging register. The MOS probes
  a Tube at $FCE0 at every reset (10.2).
- **JIM $FD00-$FDFF**: 256-byte window into expansion memory paged by
  $FCFF [AUG5 p237].

### 1.3 Unmapped reads [UNCERTAIN]

- FC/FD pages and pages 0-7/12-15 with nothing fitted: FPGA leaves the
  bus undriven; MAME returns $FF (it ANDs all sources from $FF); CLK
  returns $FF for empty ROM slots.
- Write-only SHEILA registers (FEx1-FEx3, FEx5-FExF): MAME's comment says
  "The Issue 4 ULA returns data from OS ROM, whereas Issue 6 ULA will
  return 0xfe" but its code returns $FF; TECH says a read gives "the
  underlying ROM byte"; FPGA drives a placeholder for any $FExx read.
- The MOS never reads the write-only registers. Its two probes give
  "nothing there" with either $FF or a floating-bus value: a Tube is
  assumed only if $FCE0 bit 0 reads 1 after writing $81 and 0 after
  writing $01 ($D9F6-$DA14), and a ROM only if "\0(C)" is at
  $8000+[$8007] ($D98E-$D99D) [derived]. $FF is a safe choice.

### 1.4 The ROM files

`os_basic.ic2` (32K, SHA-1 4a66c83aba07d0a8e76ed8a5545af04e11c19fdc,
MAME's `ROM_LOAD`): offset $0000-$3FFF = BASIC at $8000-$BFFF (pages 10
and 11), $4000-$7FFF = MOS 1.00 at $C000-$FFFF [MAME `ROM_START(electron)`;
derived from the vectors]. `adfs.rom` (16K, SHA-1
e7c7a1094d50a3579751df2007269067c8ff6812 = MAME's `adfs100`): "Acorn
ADFS 1.00", service ROM type $82, entry $8003 [ADFS $8000-$8018].

---

## 2. The ULA's registers and interrupts

### 2.1 Register map

| Address | Read | Write | Source |
|---|---|---|---|
| $FE00 | interrupt status | interrupt enable (bits 6-2) | AUG5 p211-215; FPGA; MAME; CLK [confirmed] |
| $FE01 | (unmapped) | (none) | AUG5 p210 (not listed) |
| $FE02 | (unmapped) | screen start, low | AUG5 p216 |
| $FE03 | (unmapped) | screen start, high | AUG5 p216 |
| $FE04 | cassette receive shift register | cassette transmit shift register | AUG5 p218-219 |
| $FE05 | (unmapped) | interrupt clear + ROM page | AUG5 p219-220 |
| $FE06 | (unmapped) | counter: sound divider / cassette | AUG5 p221-222 |
| $FE07 | (unmapped) | misc control: comms mode, display mode, motor, CAPS LED | AUG5 p222-223 |
| $FE08-$FE0F | (unmapped) | palette | AUG5 p223-224 |

All mirrored at $FEx0-$FExF (1.2).

### 2.2 $FE00

Read (status) [AUG5 p211-212 figure 14.1; FPGA `isr_data`; MAME;
CLK] [confirmed]:

| Bit | Meaning |
|---|---|
| 7 | always 1 |
| 6 | high tone detect |
| 5 | transmit data empty |
| 4 | receive data full |
| 3 | real-time clock (50 Hz) |
| 2 | display end |
| 1 | power-on reset (not an IRQ source) |
| 0 | master IRQ: OR of (status AND enable) over bits 6-2 |

Bits 6-2 show the condition whether or not it is enabled [AUG5 p212].
Bit 1 is set at power-on and cleared by the first read of $FE00; that
read still returns it set [AUG5 p215 "Clears automatically on read";
FPGA clears it after the read; MAME `io_r`; CLK] [confirmed]. It stays
clear through BREAK, which is how the MOS tells power-on from BREAK
(section 10).

Write (enable): bits 6-2 enable the matching sources; bits 7, 1, 0 have
no effect [AUG5 p212; TECH "Writing values to the two lowest bits
appears to have no effect"; FPGA (enable register holds bits 6-2 only); MAME masks `~0x83`]
[confirmed]. (CLK stores bit 1 too, so a write with bit 1 set while the
power-on flag is up would raise IRQ there [CLK `evaluate_interrupts`].)
The IRQ output is a level: asserted while any enabled status bit is set
[FPGA; MAME; CLK; AUG5 p137 "most devices ... continue to signal an
interrupt until they have been serviced"] [confirmed].

### 2.3 $FE05 (write only)

[AUG5 p219-220 figure 14.4; FPGA; MAME; CLK] [confirmed]

| Bit | Meaning on write of 1 |
|---|---|
| 7 | clear NMI: give the RAM back to the video (2.6) |
| 6 | clear high tone detect |
| 5 | clear RTC |
| 4 | clear display end |
| 3-0 | ROM page, under the rule of 1.1 |

Receive full and transmit empty are not cleared here: RxFull clears on
reading $FE04, TxEmpty on writing $FE04 [AUG5 p215 table] [confirmed by
FPGA, MAME, CLK].

### 2.4 $FE07 (write only)

[AUG5 p222-223 figure 14.6; FPGA; MAME; CLK] [confirmed]

| Bits | Meaning |
|---|---|
| 7 | CAPS LOCK LED, 1 = on |
| 6 | cassette motor relay, 1 = on |
| 5-3 | display mode 0-6; 7 behaves as mode 4 [TECH; FPGA] [confirmed] |
| 2-1 | comms mode: 00 cassette input, 01 sound, 10 cassette output, 11 "not used" |
| 0 | unused |

The comms modes are exclusive: sound only in 01 (section 5). Entering
cassette output (10) sets RxFull, a quirk Southern Belle needs [AUG5
p212 "This interrupt can occur spuriously ... when switching to tape
output mode"; FPGA comment; MAME comment] [confirmed]. In mode 11 sound
is off [MAME; CLK speaker on only for 01] [confirmed]. The MOS keeps a
RAM copy of $FE07 at $0282 [derived, MOS $D96D, $CB43, $E943].

### 2.5 Interrupt sources and timing

Line numbers count from the first pixel line (line 0); "c" is the 2 MHz
cycle within a line, c = 0 at the first pixel, 128 cycles a line
(section 3.1).

| Bit | Source | Raised | Cleared | Source |
|---|---|---|---|---|
| 6 | high tone | after 10 bit-times of continuous 1s (20 cycles of 2400 Hz) on the cassette input, only with the motor bit on, comms mode 00 and $FE06 = 0 | $FE05 bit 6 | AUG5 p212 [one source for the conditions; see 6.1] |
| 5 | transmit empty | right after bit 7 of a byte has been sent, before the stop bit; its normal state is set | write $FE04 | AUG5 p212 [confirmed: FPGA, TECH] |
| 4 | receive full | as soon as bit 7 is shifted in | read $FE04, or the next start bit | AUG5 p212 [UNCERTAIN on clearing, 6.1] |
| 3 | RTC, 50 Hz | line 99 of each field; position in the line differs by field: 31 us after the active start in one field (c = 62), 1 us before it in the other (c = -2); exactly 8192 us after the end of the 160 us vsync | $FE05 bit 5 | AUG5 p213-214 (scope measurements) [one source for the in-line position]; line 99: FPGA `v_rtc`, CLK `v_rtc = 99` [confirmed] |
| 2 | display end | at the start (falling edge) of HS after the last displayed line: line 255 in modes 0, 1, 2, 4, 5; line 249 in modes 3, 6 (the row's two blank lines count); c = 96 | $FE05 bit 4 | AUG5 p213-214; FPGA `v_disp_gph/txt`; CLK `hsync_start` [confirmed] |
| 1 | power on | power-up | read of $FE00 | 2.2 |

RTC and display end are 10 ms apart; the MOS counts both, giving its
100 Hz clock [AUG5 p213; SD-CONT p120438 "an interrupt every 10ms";
derived, MOS $DB8C-$DBC5: both paths fall into the TIME update]
[confirmed]. Older notes put display end at the end of line 255 in all
modes and RTC after line 100 [TECH]; MAME raises display end at line 250
or 256 and RTC at line 100 (its own TODO: "improve interrupt timing").

### 2.6 IRQ and NMI

- The ULA's IRQ output drives the 6502 IRQ, wire-ORed (open collector,
  3K3 pull-up in the ULA) with expansion IRQs [AUG5 p229-230; SM 6]
  [confirmed]. The MOS reads $FE00; if bit 0 is clear it offers the IRQ
  to paged ROMs (service call 5) and then IRQ2V [derived, MOS
  $DB55-$DB7F].
- NMI comes only from the expansion bus (pin 16, open collector, negative
  edge) [AUG5 p229]. The ULA watches it: an NMI gives the CPU RAM access
  "regardless of screen mode", the screen showing snow, until software
  writes $FE05 bit 7 [SM 6 "uses the NMI input to determine whether the
  processor should have full 1MHz access to the RAM regardless of screen
  mode"; AUG5 p136, pp219-220, p229, p232] [confirmed]. The latch-until-
  bit-7 detail is AUG5's [one source]. FPGA, MAME and CLK do not model
  it. No standard Acorn hardware pulls NMI (the Plus 3 does not, 9.3), so
  it matters only for third-party disc/Econet boards.
- The MOS puts RTI ($40) at $0D00 at every reset, so a stray NMI returns
  [derived, MOS $D8D2-$D8D4].

### 2.7 State at reset

The MOS rewrites every register at reset (10.2), so only $FE00 bit 1
matters to it. For the record: FPGA clears status, enable, screen start,
page and sets comms mode 01 on its reset input; MAME sets screen start
$3000 and mode 0; CLK starts with status $A2 (bit 7, transmit empty,
power on). [UNCERTAIN whether BREAK, as opposed to power-on, resets the
ULA's registers: SM 6 says the POR input "resets parts of the ULA"; FPGA
resets them on its RST input.]

---

## 3. Display

### 3.1 Frame and line

| Item | Value | Source |
|---|---|---|
| Pixel clock | 16 MHz | AUG5 p228; SM 8.2 [confirmed] |
| Line | 64 us = 1024 x 16 MHz = 128 x 2 MHz = 64 x 1 MHz | FPGA `h_total`; CLK `h_total`; TECH; SM 5.1 "40 microseconds of each 64" [confirmed] |
| Active part | 40 us (c = 0-79); 640 pixel clocks | SM 5.1; AUG5 p231; FPGA `h_active`; CLK [confirmed] |
| Front porch, HS, back porch | about 8 us, 4 us, 12 us (c = 80-95, 96-103, 104-127) | AUG5 p213 (scope); FPGA/CLK `hsync_start` 768, `hsync_end` 832 [confirmed] |
| Fields | interlaced: 312.5 lines a field, 625 a frame, 50 Hz fields (20.000 ms) | SM 3.1 "625-line 50Hz, interlaced"; AUG5 p214 (RTC differs between odd and even fields); CLK alternates 312- and 313-line fields [confirmed] |
| Displayed lines | 256 (modes 0-2, 4, 5); 250 (modes 3, 6: 25 rows of 10) | SM 3.1; AUG5; FPGA; CLK [confirmed] |
| VSYNC | 160 us (2.5 lines), lines 281.0-283.5 in one field, 281.5-284.0 in the other | FPGA (a comment gives the real ULA's VSYNC as lines 281 to 283/284; FPGA itself moves it 7 lines up); AUG5 p214 160 us; positions [derived] from RTC = VS end + 8192 us = line 99 |

For an emulator: alternate fields of 312 lines (39936 cycles; RTC at c =
-2 of line 99) and 313 lines (40064 cycles; RTC at c = 62 of line 99);
the picture content is the same in both fields, the video address being
reloaded at each field's start [derived from CLK `v_total()`, its RTC at
h = 0 / h_half, and AUG5's positions]. CLK places RTC 1 us later (c = 0
and c = 64). TECH, MAME and FPGA's default output use 312 lines (50.08 Hz)
[TECH "312 scanlines a frame ... 50.08"; MAME `set_raw(16_MHz_XTAL, 1024,
0, 640, 312, 0, 256)`; FPGA mode "00"]: right for the line length, wrong
for the field rate.

Where the picture sits [derived from the table]: 29 lines (or 28.5)
between the end of VSYNC and line 0, 25 lines from line 256 to VSYNC;
horizontally 16 us from the start of HS to the first pixel. Outside the
active area, and on the blank lines, the output is black (no border
colour) [FPGA (border and blanking output black); MAME fills black;
CLK outputs blank] [confirmed].

### 3.2 Modes

| Mode | Pixels | Colours | Bits/pixel | Bytes a line | Lines | Text | Screen | Base (wrap target) |
|---|---|---|---|---|---|---|---|---|
| 0 | 640 x 256 | 2 | 1 | 80 | 256 | 80 x 32 | 20K $5000 | $3000 |
| 1 | 320 x 256 | 4 | 2 | 80 | 256 | 40 x 32 | 20K | $3000 |
| 2 | 160 x 256 | 16 | 4 | 80 | 256 | 20 x 32 | 20K | $3000 |
| 3 | 640 x 200 in 25 rows of 8 + 2 blank lines | 2 | 1 | 80 | 250 | 80 x 25 | 16K $4000 | $4000 |
| 4 | 320 x 256 | 2 | 1 | 40 | 256 | 40 x 32 | 10K $2800 | $5800 |
| 5 | 160 x 256 | 4 | 2 | 40 | 256 | 20 x 32 | 10K | $5800 |
| 6 | 320 x 200 in 25 rows of 8 + 2 blank lines | 2 | 1 | 40 | 250 | 40 x 25 | 8K $2000 | $6000 |
| (7) | as mode 4 | | | | | | | |

Sources: [SM 3.1 (modes, sizes); FPGA `mode_base`, `mode_40`,
`mode_text`, `mode_bpp`; CLK `VideoOutput::write`; MAME `mode_size`,
`mode_bpp`, `mode_max_ra`; TECH "&3000 for modes 0,1 and 2, &4000 for
mode 3, &5800 for modes 4 and 5, and &6000 for mode 6"] [confirmed].
80-byte modes fetch one byte every 0.5 us, 40-byte modes every 1 us
[derived from 640 pixel clocks / bytes]. The MOS turns MODE 7 into mode 6
[derived, MOS $DADA-$DAE6]. The Electron starts in mode 6: the hard-reset
startup byte $028F = $FF selects mode 7, i.e. 6 [derived, MOS $D92D,
$D965-$D970, $DADA].

### 3.3 Fetch order, rows, start address, wrap

- **Layout**: character cells, as the BBC Micro: within a row the
  address goes up by 8 per byte column; within a cell, by 1 per pixel
  line. A pixel line's byte k is at `row_start + 8 k + line` (line 0-7);
  the next row starts at `row_start + 640` (80-byte modes) or `+ 320`
  [FPGA (address = byte counter with the row's line number in bits 2-0); CLK
  `ram_[byte_addr_ | char_row_]`, `byte_addr_ += 8`; MAME `screen_update`
  (+0x278 / +0x138 after 8 lines)] [confirmed].
- **Modes 3 and 6**: the line counter runs 0-9; lines 8 and 9 of each
  row are blank (black) and fetch nothing [FPGA (lines 8, 9 blanked);
  CLK `in_blank()`; MAME `m_screen_ra >= 8`; SD-CONT p120415 "in mode 3
  there are only 200" active lines] [confirmed]. FPGA's notes say that,
  per the ULA schematics, the counter's bit 3 (VA3) is forced to 0 in
  graphics modes, and that 0xC0DE's demo Vertical Rupture depends on it
  [one source]: switching from a text to a graphics mode mid-row restarts
  the 0-7 count.
- **Screen start**: 15-bit address A14-A6 (64-byte steps); A5-A0 = 0.
  $FE02 bits 7-5 = A8-A6 (bits 4-0 ignored); $FE03 bits 5-0 = A14-A9
  (bits 7-6 ignored) [AUG5 p216 figure 14.2; FPGA; CLK; MAME;
  TECH] [confirmed]. The MOS writes FE03 = hi >> 1 and FE02 = (hi bit 0,
  lo) >> 1 [derived, MOS $C9F6-$C9FD]. The ULA latches it once a field,
  at the end of the field's last line; writes during a field act from
  the next one [TECH "read once at the start of frame"; FPGA; CLK;
  SD-CONT p120508 (FireTrack needed this)] [confirmed].
- **Wrap**: when the address counter steps past $7FFF (15-bit overflow:
  A14-A11 all 0) it continues at the mode's base, i.e. it subtracts the
  screen size: $8000 becomes $3000 in modes 0-2, $4000 in mode 3, $5800
  in modes 4, 5, $6000 in mode 6 [FPGA (replaces the address with the
  mode base plus its low bits); CLK; MAME `read_vram` subtracts
  `mode_size`] [confirmed].
  The same test catches a start address below $0800 (the "start 0000
  becomes the mode base" quirk); a start between $0800 and the base is
  displayed from there [TECH] [one source, consistent with FPGA's logic].
  The MOS also wraps its own start address [derived, MOS $C9E7-$C9ED].

### 3.4 Pixels and palette

Pixels leave each byte MSB first [FPGA; CLK; MAME] [confirmed]. The
palette index (4 bits) for pixel n of a byte:

| Mode type | Pixel n uses bits | Index bits 3 2 1 0 | Logical colour |
|---|---|---|---|
| 2 colours (0, 3, 4, 6) | 7-n (n = 0-7) | b(7-n) 0 0 0 | 0 or 1 (index 0 or 8) |
| 4 colours (1, 5) | 7-n, 3-n (n = 0-3) | b(7-n) 0 b(3-n) 0 | 2 b(7-n) + b(3-n) (index 0, 2, 8, 10) |
| 16 colours (2) | 7-n, 5-n, 3-n, 1-n (n = 0-1) | b(7-n) b(5-n) b(3-n) b(1-n) | the index |

Palette registers ($FE08-$FE0F, write only). Each colour has three bits,
one per gun; a **1 turns the gun off** [AUG5 p223 "The bits are negative
logic"; FPGA stores the inverse; MAME `data ^ 0xff`; CLK `~value`]
[confirmed]. Layout for 16 colours [AUG5 p224 figure 14.7c]:

| Reg | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 | Colours |
|---|---|---|---|---|---|---|---|---|---|
| $FE08 | B10 | B8 | B2 | B0 | G10 | G8 | x | x | 0, 2, 8, 10 |
| $FE09 | x | x | G2 | G0 | R10 | R8 | R2 | R0 | |
| $FE0A | B14 | B12 | B6 | B4 | G14 | G12 | x | x | 4, 6, 12, 14 |
| $FE0B | x | x | G6 | G4 | R14 | R12 | R6 | R4 | |
| $FE0C | B15 | B13 | B7 | B5 | G15 | G13 | x | x | 5, 7, 13, 15 |
| $FE0D | x | x | G7 | G5 | R15 | R13 | R7 | R5 | |
| $FE0E | B11 | B9 | B3 | B1 | G11 | G9 | x | x | 1, 3, 9, 11 |
| $FE0F | x | x | G3 | G1 | R11 | R9 | R3 | R1 | |

The 2- and 4-colour modes use $FE08/$FE09 only, the same bits: 2 colours:
colour 0 = (R FE09.0, G FE09.4, B FE08.4), colour 1 = (FE09.2, FE08.2,
FE08.6) [AUG5 p224 figure 14.7a]; 4 colours: R0-R3 = FE09 bits 0-3,
G0 G1 = FE09 bits 4, 5, G2 G3 = FE08 bits 2, 3, B0-B3 = FE08 bits 4-7
[AUG5 figure 14.7b]. MAME `update_palette`, CLK `set_palette_group` and
FPGA's colour case implement exactly this table [confirmed]. Eight
physical colours (RGB, one bit each) [SM 6 "RED, GREEN, BLUE: These TTL
outputs"] [confirmed].

### 3.5 Cursor and flashing

No hardware cursor or flashing: both are software. The MOS flashes
colours by rewriting the palette from the display-end interrupt
[AUG5 p223 "The cursor and flashing colours are entirely generated in
software"; derived, MOS $DB98-$DBB8 (flash counter $0251, calls $C8A3)]
[confirmed].

### 3.6 Changes during a field [UNCERTAIN]

- FE07 mode changes: TECH's two texts disagree ("Mode changes take effect
  at the end of the current scanline" vs the MAME-quoted ElectrEm note
  "Mode changes are 'immediate'"); FPGA applies them at once (bits per
  pixel, 40/80 bytes, text/graphics on the next pixel). hoglet wrote in
  2015 that the ULA "samples the screen start address, and the mode base,
  once per field" [SD-CONT p120508], but FPGA's current code uses the mode
  base live at each wrap. Games that mix modes in a frame: FireTrack
  (scroll), Flight Path 737 (40 and 80-byte modes) [TECH; SD-CONT].
- Palette writes act at once [MAME-quoted ElectrEm note; FPGA]
  [confirmed].

---

## 4. CPU timing and contention

### 4.1 Rules by address

| Access | CPU clock | Source |
|---|---|---|
| ROM: MOS, BASIC (pages 10/11), external paged ROM | 2 MHz: PHI OUT low 250 ns, high 250 ns | AUG5 p231 15.3.1, p229; SM 5.1; FPGA `rom_access`; CLK; MAME [confirmed] |
| RAM, any mode, any time | 1 MHz: 1 us, or 1.5 us when the previous cycle left the 1 MHz clock mid-cycle | SD-CONT p120438 (scope: "Regardless of the mode, RAM accesses always drop down to 1MHz"); SM 5.1 "1MHz during an access cycle to the RAM"; FPGA; AUG5 p231 [confirmed] |
| RAM in modes 0-3 during the 40 us active part of a pixel line | clock held until that part ends, then the 1 MHz access | SM 5.1; AUG5 p231-232; SD-CONT p120438; FPGA [confirmed] |
| SHEILA $FE00-$FEFF | 1 MHz, never stalled | AUG5 p229, p231 ("peripherals ... free to do in the normal way"); FPGA `io_access`; CLK `run_until_io_slot` [confirmed] |
| FRED/JIM $FC00-$FDFF | 1 MHz, never stalled | AUG5 p229 ("750 ns or 1250 ns when accessing the 1 MHz bus"), p237; FPGA [confirmed; CLK runs them at 2 MHz] |
| Keyboard (pages 8/9) | [UNCERTAIN] 1 MHz per FPGA (keyboard reads count as I/O there) and MAME (`set_clock_scale(0.5)`); 2 MHz per CLK and TECH ("when accessing the ROM or keyboard areas ... 2Mhz") | |

Every 6502 bus cycle counts, dummy reads and writes included: a 2-cycle
SEI fetched from RAM costs two RAM accesses [derived; consistent with the
9 us loop below].

Superseded: TECH's "Access to RAM at any other time is at 2Mhz" (modes 0-3
outside the active window) was disproved by the scope measurement
[SD-CONT p120438]; CLK still gives such accesses 1 cycle when aligned
(`run_until_ram_slot`), MAME gives 1 MHz.

### 4.2 The 1 MHz slots

The ULA has internal 2 MHz and 1 MHz clocks; a RAM or I/O access must end
with a 1 MHz cycle. Going from 2 MHz to 1 MHz the first PHI OUT cycle is
"low 250 ns, high 750 ns" or "low 250 ns, high 1250 ns" by the phase of
the two clocks [AUG5 p231 15.3.2 (and p229), quoted in SD-CONT p120446].
Back-to-back RAM accesses take exactly 1 us each ("low for 0.25us and
high for 0.75us") [SD-CONT p120438]. Measured: the loop `SEI / LDA &C000 /
JMP &2000` at $2000 takes exactly 9 us on a real Electron [SD-CONT
p120446]; that is 8 RAM cycles + 1 ROM cycle, so 8 x 1 us + 0.5 us + one
0.5 us realignment after the ROM read [derived]. FPGA's clock logic
(stop at 16 MHz tick 6 of an 8-tick cycle on RAM/I/O; release on the 1 MHz
boundary) gives this [derived from FPGA]. The 1 MHz phase is fixed to the
line (1024 / 16 = 64 slots a line) [derived].

### 4.3 Contention in modes 0-3

- Contended lines: the 256 pixel lines of modes 0, 1, 2; in mode 3 only
  the 200 pixel lines (lines 0-7 of each 10-line row), not the two blank
  lines of each row nor lines 250-255; never the other 56 (or 112) lines
  of the field [SD-CONT p120415 (hoglet) and the matching measured mode 3
  times; FPGA `contention` only where pixels are drawn; CLK `in_blank()`;
  SM 5.1 "40 microseconds of each 64 microseconds of the 256 lines in
  312"] [confirmed]. (ElectrEm and MAME's comment say "mode 3 only 250
  scanlines are affected"; MAME's TODO admits it.)
- Window: the 40 us active part. Longest measured PHI OUT stretch: low
  0.25 us + high 41.25 us = a 41.5 us cycle [SD-CONT p120438].
- Modes 4-6 never stall (each RAM access 1 us or 1.5 us) [SD-CONT
  p120438 "In Modes 4-6, RAM accesses are never extended"] [confirmed].
- ROM and I/O cycles run normally during the window [AUG5 p231]
  [confirmed]; code in ROM keeps running until it touches RAM (zero page
  and stack count).
- An NMI lifts the stall (2.6).

### 4.4 A model for the emulator [derived]

In 2 MHz cycles, c = 0 at the first pixel of a line (c 0-127), slots of
two cycles starting at even c:

1. ROM access: 1 cycle.
2. RAM or I/O access starting at cycle c: it occupies the first slot
   [2k, 2k+2) with 2k >= c, so it lasts 2 cycles when c is even and 3
   when odd.
3. In modes 0-3, for RAM only, on a contended line: slots starting at
   c = 0-78 are taken by the video; the access waits for the slot at
   c = 80 and ends at c = 82.

This gives 1 us / 1.5 us accesses, the 9 us loop, and a 41.5 us maximum
(an access starting at c = -1, i.e. 127 of the previous line, ends at
c = 82: 83 cycles). [UNCERTAIN: the slot phase (CLK puts slot ends at odd
c, ending stalls at c = 81, which gives a 41 us maximum) and the window
edges to one cycle; FPGA synchronises the video's contention signal
through two 16 MHz flip-flops.] Validate with the loop times below.

### 4.5 Measured figures

`10 INPUT M / 20 MODE M / 30 TIME=0 / 40 FOR A=0 TO 10000:NEXT / 50 PRINT
TIME` (seconds) [SD-CONT]:

| Machine | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| Issue 2 (hoglet, p120398) | 14.71 | 14.74 | 14.89 | 11.92 | 7.07 | 7.08 | 7.07 |
| Issue 4, no Plus 1 (davidb, p120419) | 14.71 | 14.74 | 14.90 | 11.94 | 7.08 | 7.08 | 7.08 |
| Issue 4 with Plus 1 (davidb, p120418) | 17.04 | 17.09 | 17.30 | 13.41 | 7.58 | 7.57 | 7.57 |
| FPGA after the fix (p120458) | 14.70 | 14.72 | 14.87 | 11.92 | 7.07 | 7.08 | 7.07 |
| Elkulator, 2015 (p120398) | 17.26 | 17.26 | 17.50 | 16.67 | 7.08 | 7.08 | 7.07 |
| ElkJS, 2015 (p120423) | 13.01 | 13.08 | 13.16 | 11.02 | 7.08 | 7.09 | 7.08 |
| ElectrEm, 2015 (p120398) | 13.61 | 13.64 | 13.76 | 13.61 | 6.71 | 6.72 | 6.71 |

Two real machines agree [confirmed]. With `A%` and a Plus 1: 5.82, 5.83,
5.90, 4.61, 2.62, 2.63, 2.63 [SD-CONT p120428, one machine]. Why a Plus 1
slows the loop by about 16% in modes 0-3 is not explained [UNCERTAIN].

---

## 5. Sound

- One square-wave tone from the ULA's SOUND O/P pin (3 V p-p through 1K)
  to a two-transistor amplifier and a 16 ohm speaker; no volume control
  [AUG5 p228; SM 3.7, 8.10] [confirmed]. FPGA, MAME and CLK all produce a
  50% square wave [confirmed].
- Frequency = 1 MHz / (32 x (S + 1)), S = $FE06 (0-255): 122 Hz to
  31.25 kHz [AUG5 p221; MAME `set_sound_frequency((clock() / 16) / (32 *
  (data + 1)))`] [confirmed]. The MOS and the User Guide agree [derived,
  MOS $EA1F-$EA7C, table at $EA7E]: for SOUND pitch P, with n = P >> 2,
  octave = n div 12, semitone = n mod 12 and q = P AND 3, the MOS writes
  S = ((T[semitone] - (q + 1) d) >> octave) minus 1, where T = 256, 240,
  227, 214, 203, 191, 181, 170, 160, 151, 143, 135 and d = 3 below
  semitone 7, else 2. EUG pp117-118 calls P = 0 "the B one octave and a
  semitone below middle C" and P = 52 middle C; the formula gives S = 252,
  123.5 Hz (B2 = 123.47 Hz) and S = 117, 264.8 Hz (C4 = 261.6 Hz). A 16 x
  divisor would be an octave high. FPGA's source comments give a 16 x
  (S + 1) formula while its counter divides by about 32 S (124.0 Hz for
  P = 0); CLK uses 32 (S + 0.25) (123.9 Hz). The B2 match favours S + 1
  [derived].
- On only in comms mode 01 ($FE07 bits 2-1) [AUG5 p223; FPGA; MAME; CLK]
  [confirmed]. Cassette in or out (00, 10) silences it, so the machine
  cannot play sound while loading [AUG5 p223]. The motor bit (6) is
  independent. The MOS selects 01 to play and 00 for silence
  (amplitude 0), keeps $FE07's copy in $0282, and leaves $FE07 alone while
  the cassette system is busy ($EA set) [derived, MOS $E92D-$E94D,
  $E9C3-$E9D4].
- Writing $FE06 with sound on: MAME restarts the wave, FPGA lets the
  current half-period finish [UNCERTAIN, minor]. TECH notes the two lowest
  values are inaudible and act as a level, usable as a 1-bit DAC (at
  31.25 and 15.6 kHz by the formula above).
- The MOS's noise channel is software: it rewrites $FE06 with
  pseudo-random values at each sound tick [derived, MOS $E950-$E970].

---

## 6. Cassette and UEF

### 6.1 The ULA's cassette interface

- **Modes** ($FE07 bits 2-1): 00 input, 10 output; motor relay bit 6
  [AUG5 p222-223] [confirmed]. Input needs $FE06 = 0 ("All bits should be
  set to 0 (except for bit 7)"); in output mode $FE06 is ignored [AUG5
  p221-222]. The MOS writes $FE06 = 0 before reading [derived, MOS $FACF].
- **Encoding**: 1200 baud, CUTS tones: a 0 bit is one cycle of 1200 Hz, a
  1 bit two cycles of 2400 Hz; each byte: start bit 0, 8 data bits LSB
  first, stop bit 1 [SM 3.1, 3.5 "1200 baud using the CUTS standard
  tones"; TECH "Tape Input/Output"; UEF "Notes on pseudo-code"; AUG5 p218
  figures 14.3a/b] [confirmed]. The ULA's own output frequencies come
  from 16 MHz / 13 (pin "÷13 IN", 812 ns period, from a 74LS169):
  1201.9 Hz and 2403.8 Hz (/1024 and /512) [SM 6, 8.8; AUG5 p228
  "approximately 1200 Hz if divided by 1024"; FPGA's output counter gives
  the same two frequencies] [confirmed]. Output is a synthesised sine, 1.8 V p-p at the
  ULA pin [SM 6, 8.8].
- **$FE04 receive** (read): bits enter at bit 7 and move towards bit 0;
  after a byte the register holds it in normal order. RxFull is set as
  soon as data bit 7 is in; cleared by reading $FE04 [AUG5 p212, p218
  figure 14.3a; TECH (says the original AUG had the direction reversed);
  MAME; CLK] [confirmed]. Also cleared, says AUG5, when the next start
  bit arrives, leaving a two-bit (1.66 ms) window; CLK clears it after
  one more bit (the stop bit); TECH: cleared "when the next bit comes
  along"
  [UNCERTAIN]. The MOS reads it inside the interrupt [derived, MOS
  $F4FF]. A byte not read in time is lost [AUG5 p218].
- **$FE04 transmit** (write): bit 0 goes first; TxEmpty is set after bit
  7, before the stop bit, and cleared by the next write; while it stays
  set the ULA sends continuous 1s (high tone) [AUG5 p212, p219; TECH;
  FPGA (2400 Hz whenever no byte is being sent)] [confirmed].
- **High tone**: AUG5: 10 successive 1 bits (20 cycles of 2400 Hz), only
  with the motor bit on [AUG5 p212]; CLK: the 10-bit receive window all
  1s; MAME: a start bit of 1 and byte $FF; the real ULA uses an external
  RC on pin "CAS RC" to detect "a continuous period of high tone" [SM 6];
  FPGA raises it on the first 2400 Hz half-cycle after silence or a byte
  [UNCERTAIN in the details; 10 bit-times of 1s is the common ground].
- **Interrupts**: high tone (6), transmit empty (5), receive full (4)
  (2.5). The MOS dispatches all three to $F4DF [derived, MOS $DB68-$DB6C].
- With the screen in modes 0-3 the 6502 can be held off RAM for up to
  40 us, and AUG5 advises modes 4-6 for cassette work (worst-case IRQ
  delay 10 ms in modes 0-3, 4 ms in 4-6) [AUG5 p218, p232].

### 6.2 What the MOS does

[derived from the MOS unless marked]

- Start reading ($FACD): $FE06 = 0; IRQ enables = old & $DF | $50
  (receive full and high tone on, transmit empty off); $FE07 = copy &
  $F9 | $40 (motor on, comms 00).
- Start writing ($FABF): enables = old & $AF | $20 (transmit empty on);
  $FE07 = copy & $F9 | $44 (motor on, comms 10).
- Stop ($FAB0): enables &= $8F. Comms back to 00 at $FA9C-$FAA4.
- Interrupt ($F4DF): if transmit empty and writing, write the next byte
  ($BD) to $FE04; else read $FE04, write $40 to $FE05 (clear high tone)
  and step a state machine in $C2: 1 wait for high tone, 2 wait for the
  sync byte $2A (anything else restarts at $FACD), 3 header (each byte
  handed to the foreground in $BD), 4 data (CRC per byte at $F710, stored
  through ($B0) or to the Tube at $FCE5), 5 the two CRC bytes, then stop
  ($FAB0); high tone in states 4/5 also stops. The block header is kept
  at $03B2-$03D0 as on the BBC (the block length at $03C8 is used at
  $F55B) [MOS; BBCAUG p280 for the layout].
- **Block format** (identical to the BBC's) [BBCAUG pp347-348; AUG5
  pp176-177 (the *ROM variant); MOS] [confirmed]: high-tone leader; $2A;
  filename 1-10 characters; $00; load address (4, low first); execution
  address (4); block number (2); block length (2); block flag (1: bit 0
  protected, bit 6 empty, bit 7 last block); 4 spare bytes; header CRC
  (2, **high byte first**, over the filename to the spare bytes, not the
  $2A); data (block length, normally 256); data CRC (2, high first).
- **CRC**: CRC-16, polynomial $1021, initial 0, MSB first (XMODEM) [derived
  from MOS $F710-$F72E and the routine in AUG5 pp180-181; WD177x gives the
  same polynomial for disc CRCs] [confirmed].

### 6.3 UEF

[UEF] = draft 0.10, document draft 25 (Harte, Ross, 2005).

- Header: "UEF File!" + $00 (10 bytes), minor version, major version.
  Then chunks: ID (2 bytes, little-endian), length (4, LE), data. Files
  are often gzip-compressed with the same extension; test for $1F $8B
  [UEF "Header and Chunk Template"; MAME `uef_cas.cpp` (`GZ_HEADER`);
  CLK `gzopen`] [confirmed]. Tape chunks are processed in file order.
- Floats (&0113, &0116): 4-byte IEEE-like, little-endian, decoding code
  in the spec [UEF].

| Chunk | Meaning | Source |
|---|---|---|
| &0100 | data bytes with implicit start 0 and stop 1 (8N1), each byte LSB first | UEF; MAME; CLK [confirmed] |
| &0101/&0103 | multiplexed copy of the previous block; ignore | UEF |
| &0102 | explicit bits: first byte = number of unused bits at the end; then bits, LSB of each byte first | UEF; MAME; CLK [confirmed] |
| &0104 | defined format: data bits, parity 'N'/'E'/'O', stop bits (negative = -2n waves); start bit always 0 | UEF; CLK |
| &0110 | carrier (high tone): 16-bit count of 2400 Hz cycles at 1200 baud (units of 1/(2 x baud) s) | UEF; MAME (one 2400 Hz cycle a count) [confirmed]; CLK queues a whole 1 bit (two cycles) a count [CLK differs] |
| &0111 | carrier, dummy byte $AA (as 0,0,1,0,1,0,1,0,1,1), carrier: two 16-bit cycle counts | UEF; CLK |
| &0112 | gap (silence): 16-bit length in 1/(2 x baud) s | UEF; MAME [confirmed]; CLK uses 1/baud [CLK differs] |
| &0113 | change of base frequency: float, bits a second (modal; starts at 1200) | UEF; CLK |
| &0114 | security waves: 24-bit count, 'P'/'W' first/last flags, one bit a wave (0 short, 1 long), MSB first in CLK | UEF; CLK |
| &0115 | phase change (ignorable) | UEF |
| &0116 | gap, float seconds | UEF; MAME; CLK [confirmed] |
| &0117 | data encoding: 16-bit baud, 300 or 1200; at 300 every bit is four times longer (0 = four 1200 Hz cycles, 1 = eight 2400 Hz cycles) | MAME, CLK [confirmed]; not in draft 0.10 |

The spec allows dropping wave-level detail (&0114, phase) and rounding
&0104's stop bits "while retaining 99.9% compatibility" [UEF]. 300-baud
chunks are BBC tapes; the Electron MOS and ULA only do 1200 [TECH "Data
is always saved at 1200 baud"; AUG5 p222].

### 6.4 Feeding tapes to the ULA

- **Bit level, as a waveform**: MAME renders the UEF to a 4800 Hz
  square wave (4 samples a 1200 baud bit) and its ULA samples it at
  4800 Hz, matching patterns to bits, then bytes, then RxFull/high tone
  [MAME `uef_cas.cpp`, `electron_ula.cpp` `tape_timer_handler`]. CLK turns
  chunks into pulses, recovers bits with a digital PLL, and shifts them
  into a 10-bit receive register that raises RxFull and high tone [CLK
  `TapeUEF.cpp`, `Parsers/Acorn.cpp`, `Electron/Tape.cpp`]. FPGA times the
  input's high pulses at 16 MHz, splitting at 5000 ticks (1200 Hz half
  cycle 6666, 2400 Hz 3333) [FPGA]. For this emulator the simplest
  faithful path is bit level: from the UEF produce bits at the chunk's
  baud (833.3 us = 1666.7 CPU cycles at 1200 baud), shift them into the
  ULA receive logic while comms mode is 00 and the motor bit is on, and
  raise the interrupts as in 6.1 [derived]. Copy-protected loaders that
  read $FE04/$FE00 themselves need this path.
- **Fast load** (CLK's option, "quick_load"): NOP the MOS's BEQs at
  $F4E5, $F6DE, $F6FA and $FA51 (each tests $0247, the ROM filing system
  flag, to choose the tape path), so the cassette filing system takes the
  *ROM filing system path; then catch the opcode fetch at $F0A8 (the
  service-call dispatcher) when X = 14 (read byte) and $0247 = 0: run the
  tape to the next received byte (or to a $2A sync byte when not inside a
  block), return it in Y with A = 0 and an RTS [CLK `Electron.cpp`
  `perform_bus_operation`; addresses confirmed in the MOS: $F4E2-$F4EE,
  $F6DB-$F6E0, $F6F7-$F700, $FA4E-$FA53, $F0A8]. This skips the real-time
  byte timing (8.33 ms a byte), leaders and gaps; the MOS still parses
  headers and checks CRCs. It cannot help loaders that bypass the MOS.

---

## 7. Keyboard

- The keyboard sits in ROM pages 8 and 9: with either paged, a read of
  $8000-$BFFF returns the keyboard. Each address line A0-A13 drives one
  column; a column is selected when its line is **low**; D0-D3 return the
  four rows, **1 = pressed**; with several lines low the columns are ORed
  (diodes) [AUG5 p225; TECH "FE?5"; MAME `keyboard_r`; CLK; FPGA (KBD0-3
  inputs inverted)] [confirmed].
- High nibble: 0 per TECH, FPGA and MAME; CLK leaves the empty slot's $F
  there [UNCERTAIN]; the MOS masks with AND #$0F [derived, MOS $ECAF,
  $ECBE].

Matrix [AUG5 p225 table; MAME `INPUT_PORTS_START(electron)`; CLK
`Keyboard.hpp`; TECH] [confirmed, all four agree]:

| Col | Address | Bit 0 | Bit 1 | Bit 2 | Bit 3 |
|---|---|---|---|---|---|
| 0 | $BFFE | → ( \| \ ) | COPY ( [ ] ) | (none) | SPACE |
| 1 | $BFFD | ← ( ^ ~ ) | ↓ ( _ } ) | RETURN | DELETE |
| 2 | $BFFB | - = | ↑ ( £ { ) | : * | (none) |
| 3 | $BFF7 | 0 @ | P | ; + | / ? |
| 4 | $BFEF | 9 ) | O | L | . > |
| 5 | $BFDF | 8 ( | I | K | , < |
| 6 | $BFBF | 7 ' | U | J | M |
| 7 | $BF7F | 6 & | Y | H | N |
| 8 | $BEFF | 5 % | T | G | B |
| 9 | $BDFF | 4 $ | R | F | V |
| 10 | $BBFF | 3 # | E | D | C |
| 11 | $B7FF | 2 " | W | S | X |
| 12 | $AFFF | 1 ! | Q | A | Z |
| 13 | $9FFF | ESCAPE | CAPS LK / FUNC | CTRL | SHIFT (both) |

Characters in brackets are the shifted / CTRL ones printed on the arrow
and COPY keys [MAME `PORT_CHAR`; CLK `sequences`; EUG p19 "five keys in
the top right area ... four of the keys have arrows on them, and one of
them the word COPY"]. Both SHIFT keys share one switch [TECH].

- **CAPS LK / FUNC** is one key: alone it is FUNC (BASIC keywords and
  f0-f9 on the number keys); with SHIFT it toggles caps lock [EUG pp17-19]
  [one source]. The LED is driven by $FE07 bit 7 [AUG5 p223]; the MOS
  lights it while caps lock is on [derived, MOS $EABA-$EADA].
- **BREAK** is not in the matrix: it drives the 6502's RST (through the
  ULA, also out on expansion pin 15) [SM 6 "RST: Master reset for the
  6502: Enabled on power up and when the BREAK key is pressed"; AUG5
  p229] [confirmed]. MAME and CLK model it as the CPU reset line. Section
  10 has how the MOS tells the resets apart.
- **MOS scanning** [derived]: from the 100 Hz timer path ($DC12-$DC1F,
  via KEYV) the scan at $EC95 pages in ROM 8 ($0C then $08 to $FE05),
  reads $8000 (all columns) and stops if the low nibble is 0, else reads
  columns one by one ($ED38: $BFxx for columns 0-7, $BEFF-$9FFF for
  8-13, some preceded by a dummy zero-page read), then restores the ROM.
  A single key test (e.g. CTRL at reset) goes through $EC1E/$ECF8.

---

## 8. The Plus 1

Brief; to be completed when it is emulated.

- Adds two cartridge slots, a Centronics printer port and a 4-channel
  ADC (two joysticks, two fire buttons) [P1UG ch.1] [one source].
- ROM pages (its own latch, $FE05 writes with bits 7-4 = 0): 0, 1 slot
  SK2 (the cartridge's ROMQA line = bit 0), 2, 3 slot SK1, 12 the Plus 1's
  own ROM ("Expansion 1.00", 4K mirrored to 16K in MAME), 13 a
  "high-priority" select seen by both slots [MAME `plus1.cpp`
  `expbus_r/w`; AUG5 p236] [confirmed for the allocation].
- Registers [MAME `plus1.cpp`] [one source; AUG5 p237 confirms only the
  range $FC70-$FC73]: $FC70 ADC (ADC0844: write starts a conversion on a
  channel, read the result); $FC71 printer data (write; strobe pulsed);
  $FC72 status (read): bit 7 printer busy, bit 6 ADC end of conversion,
  bits 5, 4 fire buttons 1, 2, bits 3-0 high.
- The Plus 1 ROM changes the MOS's ROM scan order to 11 down to 0, then
  15 to 12 [AUG5 p235].

---

## 9. The Plus 3

### 9.1 What it is

A WD1770 controller, a 3.5" 80-track drive (single- or double-sided
versions, 320K or 640K), a connector for a second external drive, and
Acorn ADFS 1.00 in a 27128 ROM; it passes the bus on to a Plus 1
[P3UG pp5-6; MAME `plus3.cpp` header; P3SCH] [confirmed]. The 1770's clock
is 8 MHz (16 MHz / 2) [MAME `DERIVED_CLOCK(1, 2)`; CLK `emplace_drives(2,
8000000, ...)`] [confirmed].

### 9.2 Addresses

| Address | Read | Write | Source |
|---|---|---|---|
| $FCC0 | [UNCERTAIN] MAME $FF, CLK $01; the latch (74LS174) has no read path | drive control latch | TECH; MAME; CLK; P3SCH [confirmed for the address] |
| $FCC4 | 1770 status | 1770 command | TECH; MAME; CLK; ADFS $BA26, $BA38 [confirmed] |
| $FCC5 | track | track | same; ADFS $BC41 |
| $FCC6 | sector | sector | same; ADFS $BEFD |
| $FCC7 | data | data | same; ADFS $BA41, $BA5B |

Decode from the schematic [P3SCH] [derived, low confidence from a scan]:
page $FC with A7-A4 = 1100 and A9 = 0 selects the board; A2 = 1 the 1770,
A2 = 0 the latch; A3 (and A0, A1 for the latch) do not seem to be
decoded, so $FCC8-$FCCF may mirror [UNCERTAIN]. ADFS uses only $FCC0 and
$FCC4-$FCC7.

### 9.3 Drive control latch ($FCC0, write)

| Bit | Function | Source |
|---|---|---|
| 0 | select drive 0 (internal) | MAME `wd1770_status_w`; CLK `set_control_register`; P3SCH [confirmed] |
| 1 | select drive 1 (external) | same [confirmed] |
| 2 | side select: 1 = side 1 (head 1) | same; ADFS $BD44 [confirmed] |
| 3 | 1770 DDEN: 1 = single density (FM), 0 = double (MFM) | same [confirmed]; ADFS always writes 0 |
| 4 | inverted (7416) to link LK1, which can pull NMI | P3SCH [one source, link state unknown]; ADFS writes 0 |
| 5 | 1770 /MR: 0 holds the controller in reset | MAME `mr_w(BIT(data, 5))`; P3SCH [confirmed]; ADFS writes 1 ($21, $22) |
| 6, 7 | unused | P3SCH (six-bit latch) |

On the board each select line is ANDed with the 1770's motor-on output
(MO): a drive is selected only while the motor runs [P3SCH, 7438 gates]
[derived]. ADFS writes $21 (drive 0) or $22 (drive 1), plus 4 for side
1, and $00 on an error path [ADFS $BBB3-$BBE0, $BF20-$BF35, $BD44,
$BAA2-$BAA4].

### 9.4 No interrupts

The 1770's INTRQ and DRQ are not connected (no lines drawn on the
schematic) [TECH "simply a WD1770 drive controller without its interrupt
line connected"; P3SCH; MAME and CLK connect nothing] [confirmed]. ADFS
polls: it disables IRQs (SEI), writes the command, waits 44 cycles
(22 us), then loops on the status register: bit 0 (busy) clear means done, bit 1
(DRQ) means move one byte through $FCC7 [ADFS $BB25, $BA26-$BA5E]
[derived]. During a transfer it also blanks the palette (OSBYTE &73) and,
in modes 0-3, switches the ULA to mode 6 ($FE07 copy | $30) so RAM is never
stalled, restoring both afterwards [ADFS $BB30-$BB4F, $BB53-$BB5D]
[derived]; P3UG pp14-15 describes the visible effect (screen cleared to the
background colour in modes 0-3, clock and keyboard stopped) [confirmed].
The ADFS uses the NMI page $0D00-$0D5F only as workspace, claimed through
service calls 12/11 (OSBYTE &8F) [ADFS $BD0E-$BD22] [derived].

### 9.5 ADFS ROM page

Page 4: the Plus 3 latches "page = 4" from $FE05 writes with bits 7-4 =
0 (a 74LS260 on DB4-DB7, another on D3-D0 = 0100, into a 74LS74) [P3SCH;
MAME `plus3.cpp` `m_romsel == 4`; AUG5 p236 "4 Disc"] [confirmed]. (CLK
puts it in slot 6, its own choice.)

### 9.6 Discs and images

- Format: 80 tracks, 16 sectors of 256 bytes, MFM; 1280 sectors (320K) a
  side [P3UG p6, p102] [one source + derived]. Sector IDs 0-15 (ADFS writes
  the logical sector mod 16 to $FCC6) [derived, ADFS $BF86-$BFAD,
  $BEFB]. Drive 0 is the built-in 3.5" drive, drive 1 the external
  connector (34-way, Shugart) [P3UG; P3SCH].
- **Logical sector to physical** in Electron ADFS 1.00 [derived, ADFS
  $BF55-$BFAD, $BBC0-$BBDA]: the disc address must be < 2560; track =
  address / 16, sector = address mod 16; if track >= 80 the side is 1 and
  the track is track - 80. So a 640K disc holds sectors 0-1279 on side 0
  and 1280-2559 on side 1. At the end of track 79 on side 0 ADFS switches
  to side 1 and restores to track 0 [ADFS $BEB0-$BEE6] [derived]. The
  FluxEngine documentation quoted in CLK says the same of Acorn ADFS in
  general ("logical block numbering goes all the way up side 0 and then
  all the way up side 1") [CLK `AcornADF.cpp` comment].
- **Image side order** [UNCERTAIN]: MAME and CLK store 2-sided images
  track-interleaved (file offset = (track x 2 + side) x 4096: T0 side 0,
  T0 side 1, T1 side 0 ...) [MAME `acorn_adfs_old_format::get_image_offset`;
  CLK `AcornADF::get_file_offset_for_position`], while a dump in ADFS
  logical order would hold side 0 then side 1; CLK's comment notes that
  emulators meet both and "the side order can't be reliably autodetected".
  esp32electron uses `.adl` for interleaved images and 640 KiB `.adf`
  for images in logical sector order.
- Images used by emulators [MAME `acorn_dsk.cpp` `acorn_adfs_old_format`]:
  `.ads` 160K (40 tracks, 1 side), `.adm` 320K (80, 1 side), `.adl` 640K
  (80, 2 sides, tracks interleaved in the file: T0S0, T0S1, T1S0 ...),
  `.adf` any of these (decided by size; a 320K single-sided `.adf` is a
  plain dump in track order, where the two orders agree). Identification: total sectors at offset
  $FC (3 bytes LE: $280, $500, $A00) and "Hugo" at $201 [MAME
  `find_size`; P3UG pp102-103]. CLK reads `.adf` too [CLK `AcornADF.cpp`].
- Filesystem (old map) [P3UG pp102-105] [one source]: sectors 0 and 1 the
  free space map (start addresses in sector 0, lengths in sector 1, 3
  bytes each, up to 82 entries; total sectors at sector 0 bytes 252-254;
  checksums at byte 255; boot option at sector 1 byte 253); the root
  directory at sectors 2-6 ("Hugo" at bytes 1-4 and at the end; 47
  entries of 26 bytes). Held in RAM at $0E00-$0FFF and $1200-$16FF; ADFS
  raises PAGE to $1D00 [P3UG p52].

### 9.7 What ADFS asks of the 1770

[derived from ADFS; command bits from WD177x]

- Commands: Restore with verify `$04 | r` ($BD61), Seek with verify
  `$14 | r` with the target track in the data register ($BC08-$BC1F),
  Read Sector `$80` and Write Sector `$A0` (one sector each, no multiple,
  no settle delay; the write may set P) ($BD25-$BD38). Step rate r comes
  from the startup byte (*FX255) bits 4-5 inverted, 0 by default (6 ms)
  ($BCF0-$BD0D). The motor is left to the 1770: commands do not set h, so
  the 6-revolution spin-up and the 9-revolution motor-off apply
  [WD177x].
- Errors: status AND $1C after reads (record not found, CRC, lost data),
  AND $5C after writes (plus write protect) ($BB14-$BB1C, $BA80-$BA86).
- Register writes are checked by reading back in a loop until they
  match, so writes to track/sector/data must read back at once when not
  busy ($BC3F-$BC47, $BEFB-$BF03).
- Busy should not be read for 24 us, status bits 1-7 for 32 us after a
  command [WD177x]; ADFS waits 22 us ($BA29-$BA2C) plus a few
  instructions. An emulated 1770 that sets busy at once on a command write
  is safe [derived].
- DRQ must be served within about 23.5 us at 250 kbit/s (one byte every
  32 us) [WD177x]: the poll loop does it with IRQs off.
- Formatting is not in the ROM: the Welcome disc's EFORM utility does it
  (Write Track) [P3UG p6, p48]; *COMPACT, *CAT, LOAD and SAVE need only
  Restore, Seek, Read Sector and Write Sector [derived].
- At initialisation ADFS writes $5A to the track register, reads it back,
  and reads $FCC0 bits 0-1 ($BA0C-$BA25); the result does not gate the
  "Acorn ADFS" banner [derived].
- SHIFT-BREAK boots the disc (*OPT 4 option); CTRL-A-BREAK etc. per
  P3UG appendix B.

WD1770 summary [WD177x]: status bit 7 motor on, 6 write protect, 5
spin-up done / record type, 4 record not found (seek error), 3 CRC error,
2 track 0 (type I) / lost data (II, III), 1 index (type I) / DRQ (II,
III), 0 busy. Type I `0000hVrr` restore, `0001hVrr` seek, `001uhVrr` step,
`010uhVrr` step in, `011uhVrr` step out; 1770 step rates 6, 12, 20, 30 ms;
type II `100mhE00` read sector, `101mhEPa` write sector; type III
`1100hE00` read address, `1110hE00` read track, `1111hEP0` write track;
type IV `1101iiii` force interrupt. Settle delay 30 ms. Spin-up 6
revolutions unless h = 1. CRC polynomial x^16 + x^12 + x^5 + 1.

---

## 10. Reset and start-up

### 10.1 Power-on, BREAK, CTRL-BREAK

| Event | Hardware | MOS sees | Result |
|---|---|---|---|
| Power-on | POR RC resets parts of the ULA, which holds RST; $FE00 bit 1 set | bit 1 = 1 | break type 1; RAM cleared; banner + beep |
| BREAK | RST low while held | bit 1 = 0, CTRL up | break type 0 (soft); RAM kept unless *FX200 asks |
| CTRL-BREAK | same | bit 1 = 0, CTRL down | break type 2 (hard); variables re-initialised, RAM above $0400 kept |
| SHIFT-BREAK | same | SHIFT read later | boot from the current filing system (startup byte bit 3 flips the sense) |

Sources: [SM 6 (POR, RST); AUG5 OSBYTE &FD p84 ("0 - soft BREAK, 1 -
power up reset, 2 - hard BREAK"), &C8 p67, &FF p85; derived, MOS
$D8E8-$D92D ($FE00 bit 1, CTRL tested as key $3A through $EC1E, type into
$028D), $D8F0-$D90A (RAM clear on power-on or when $0258 asks),
$DA56-$DA6A (SHIFT)] [confirmed]. The clear loop writes 0 to $0400-$7FFF but skips byte 0 of
pages $05-$7F (an INY after each page) [derived, MOS $D8FB-$D90A].
Whether BREAK resets the ULA's own registers [UNCERTAIN, 2.7]; the MOS
rewrites them anyway.

### 10.2 MOS 1.00 reset sequence

[derived, addresses in the MOS]

1. $D8D2: $40 (RTI) to $0D00; SEI, CLD, stack $FF.
2. $D8DD: $FE00 = 0 (all ULA IRQs off). $D8E3: $FE05 = $F8 (clear NMI,
   high tone, RTC, display end; page 8).
3. $D8E8: read $FE00 bit 1: power-on or BREAK; clear RAM if power-on.
4. $D912: test CTRL (hard BREAK); set the break type $028D; on hard reset
   the startup byte $028F = $FF.
5. Initialise $0200-$02FF, zero page $E2-$FF, copy the default vectors
   from $D845 (IRQ1V = $DB47, IRQ2V = $DB89, BRKV = $DB33).
6. $D95C: CLI/SEI pair; then $D965-$D973: $FE07 = mode bits (mode 6) |
   $84 (CAPS LED on, comms 10), also written to $FE04 and saved at $0282.
7. $D976-$D97B: $FE00 = $0C (enable RTC and display end; copy at $025B).
8. $D98B-$D9E3: ROM scan, pages 0-15: page in, check "\0(C)" at
   $8000+[$8007], skip copies of a higher-numbered ROM (so BASIC is found
   once, the keyboard pages fail the check), record the type byte from
   $8006 at $02A0+n.
9. $D9E5: VDU initialisation (mode, palette, screen start), service calls,
   then the Tube probe at $FCE0 ($D9F6-$DA14).
10. $DA35: banner "Acorn Electron" (string at $C303), a beep on hard
    reset; boot handling; $DA92: choose the highest language ROM (BASIC in
    page 11); $DAD4: JMP $8000 with A = 1. BASIC prints "BASIC" and ">".

### 10.3 What the hardware must provide for the BASIC prompt

[derived from 10.2]

- RAM $0000-$7FFF; the MOS at $C000-$FFFF and BASIC in pages 10 and 11.
- $FE00 bit 1 set at power-on, cleared by the first read.
- The $FE05 page rule (the scan writes $0C then each page number; the
  keyboard scan pages 8).
- Keyboard pages returning 0 in the low nibble with no key down, and CTRL
  readable at $9FFF bit 2.
- Some value for empty pages and $FCE0 (1.3).
- Display end and RTC interrupts at 50 Hz each: the MOS's clock, flash,
  keyboard scan, sound queue and cursor run from them; the display
  ($FE02/03, $FE07, palette) to see anything. Cassette, sound and timing
  rules are not needed for the prompt. With the Plus 3: ROM page 4 and the
  $FCC0/$FCC4-$FCC7 registers; the start-up text then reads "Acorn
  Electron", "Acorn ADFS", "BASIC", ">" [P3UG appendix A p106].

---

## Sources

Tags used in the text, then where to find them. Pages are the book's
printed page numbers unless noted.

- **AUG5**: *The Advanced User Guide for the Acorn Electron*, Adrian C.
  Dickens and Mark A. Holmes, Adder Publishing 1984 (the user's "Bray,
  Dickens, Holmes" are the BBC AUG's authors); fifth edition, November
  2023, by stardot.org.uk members (3rd-5th editions add corrections and
  scope measurements; chapters 7, 11, 12, 14, 15, appendices G, H, J with
  the Electron, Plus 1 and Plus 3 circuit diagrams). PDF:
  https://archive.org/details/acorn-electron-advanced-user-guide (file
  "Electron Advanced User Guide.PDF").
- **P3SCH**: the Plus 3 circuit diagram in AUG5 appendix J (PDF page
  287), read for this document (IC14 WD1770, IC15 74LS174 latch, IC9
  74LS260, IC12 74LS74, IC17 7438, LK1).
- **SM**: *Electron Service Manual*, Acorn part 0405001 issue 2, January
  1987 (sections 3.1, 3.5, 3.7, 5.1, 6, 8.2-8.10, parts list).
  https://chrisacorns.computinghistory.org.uk/docs/Acorn/Manuals/Acorn_ElectronSM.pdf
- **P3UG**: *Acorn Electron Plus 3 User Guide* (pp5-6, 14-15, 48, 52,
  102-109).
  https://chrisacorns.computinghistory.org.uk/docs/Acorn/Manuals/Acorn_Plus3UG.pdf
- **P1UG**: *Acorn Electron Plus 1 User Guide*.
  https://chrisacorns.computinghistory.org.uk/docs/Acorn/Manuals/Acorn_Plus1UG.pdf
- **EUG**: *Acorn Electron User Guide* (keyboard pp17-19, SOUND pitch
  pp117-118).
  https://archive.org/details/acorn-electron-user-guide
- **BBCAUG**: *The Advanced User Guide for the BBC Microcomputer*, Bray,
  Dickens, Holmes, 1983 (cassette format pp347-348, workspace p280).
  https://archive.org/details/bbc-micro-advanced-user-guide
- **TECH**: Thomas Harte, "Electron Technical Information", second
  draft (ElectrEm author's notes, c. 2003; parts superseded by SD-CONT).
  http://mdfs.net/Docs/Comp/Electron/Techinfo.htm (also
  web.archive.org of electrem.emuunlim.com).
- **SD-CONT**: Stardot thread "Electron Memory Contention", September
  2015 (hoglet's scope measurements and BASIC timings; posts cited as
  p-numbers). https://stardot.org.uk/forums/viewtopic.php?p=120438
- **UEF**: "UEF [Unified Emulator Format] File Format", draft 0.10,
  document draft 25 (Thomas Harte, Fraser Ross, 2005).
  https://mdfs.net/Docs/Comp/BBC/FileFormat/UEFSpecs.htm
- **WD177x**: WD1770-00/1772-00 programming information typed from the
  Western Digital data sheets by David Gahris (1995).
  https://bio.nic.funet.fi/pub/cbm/documents/chipdata/wd177x/1772.txt
- **MAME**: github.com/mamedev/mame master, 2026-11-18, all BSD-3-Clause:
  `src/mame/acorn/electron.cpp`, `electron_ula.cpp`, `electron_ula.h`;
  `src/devices/bus/electron/exp.cpp`, `plus1.cpp`, `plus3.cpp`,
  `cart/slot.cpp`; `src/lib/formats/acorn_dsk.cpp`, `uef_cas.cpp`. (The
  old `electron_m.cpp`/`electron_v.cpp` are now `electron_ula.cpp`.)
- **CLK**: Clock Signal, github.com/TomHarte/CLK master, 2026-11-18, MIT:
  `Machines/Acorn/Electron/Electron.cpp`, `Video.cpp/.hpp`, `Tape.cpp`,
  `SoundGenerator.cpp`, `Keyboard.hpp/.cpp`, `Interrupts.hpp`;
  `Machines/Acorn/Floppy/1770.cpp`; `Storage/Tape/Formats/TapeUEF.cpp`;
  `Storage/Tape/Parsers/Acorn.cpp`; `Storage/Disk/DiskImage/Formats/AcornADF.cpp`.
- **FPGA**: David Banks (hoglet67), ElectronFpga,
  `src/common/ElectronULA.vhd` (2020), GPL-3.0, read only to confirm
  behaviour (only its behaviour is described here, no text or code is
  quoted beyond signal names); its default clocking mode, meant to match
  the original Electron, was tuned against the SD-CONT measurements.
  https://github.com/hoglet67/ElectronFpga
- **MOS $xxxx**: Acorn Electron MOS 1.00, upper half of `os_basic.ic2`
  (1.4), disassembled for this document with a small Python disassembler;
  addresses as cited.
- **ADFS $xxxx**: Acorn ADFS 1.00 (`adfs.rom`, 1.4), disassembled the
  same way.
- Not available: Paul Boddie's ULA notes (hg.boddie.org.uk/ULA, returned
  401); the Acorn "Electron Expansion Application Note" (cited in AUG5's
  bibliography, not found online).
