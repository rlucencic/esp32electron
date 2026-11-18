# esp32electron: an Acorn Electron on the MaTouch ESP32-S3 7" and the M5Stack Tab5

An Acorn Electron (1983): a 6502A at 2 MHz (1 MHz to RAM and the ULA), 32
KB RAM, the ULA (display, sound, cassette, keyboard, interrupts, ROM
paging), BBC BASIC II and MOS 1.00 in a 32 KB ROM; an Acorn Plus 3 (ADFS, a
WD1770, a 3.5" drive) to come. Both boards, as the QL. The plan: esp32emu's
CLAUDE.md, "To do" (the Electron's entry). **What's next: `TODO.md`**.

The board side (tasks, LCD, touch, SD, WiFi, BLE, setup screen, web page,
console) and the 6502 core (`m6502`) are the shared library in
`../esp32emu`; its CLAUDE.md has the runtime and the board-level lessons.
Changes there need all its projects built. An SDL desktop sibling
(`sdlelectron`) is planned: keep `components/electron` platform independent.

The machine is our own (MIT), written from Acorn's documentation (the
Electron Advanced User Guide, the service manual, the Plus 3 guide) and the
community's; emulators (MAME's Electron driver; Clock Signal, MIT; Elkulator
and B-em, GPL) only to compare behaviour against, never copied. The research
notes: `docs/electron-hardware.md`.

## Build, flash, debug (the Linux PC)

```sh
# Tab5 (/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:FB:03:03-if00)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build'
# ... -p PORT flash (writes the roms/ too), app-flash for the app only
# MaTouch (its UART port: /dev/serial/by-id/usb-1a86_USB_Serial-*, 921600)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; idf.py build && idf.py -p PORT flash'
```

- The two builds share `managed_components/`: build one after the other,
  never both at once.
- `flash` writes the ROMs into 64 KB slots of `elk_roms` (0x810000):
  `os_basic.ic2` slot 0 (32 KB: BASIC then the MOS), `adfs.rom` slot 1. A
  file of the same name in the SD card's `electron/roms` overrides one
  (`roms/README.md`: where they come from). The boards are shared with the
  other projects: after another one, a full `flash`.
- NVS: namespace `elk` (`plus3`). mDNS `acorn-electron.local`.
- The MaTouch: its UART port, not native USB (the speaker's lines); opening
  that port resets the board.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | send "cmd" |
  info` with the IDF venv's Python (`-I`). Start on the setup screen: `t
  1065 637` (Tab5), `t 850 532` (MaTouch).

Console commands (`main/main.c`): `k <n>` tap matrix key n (`electron.h`'s
`ELK_KEY_*`: column * 4 + row; 6 RETURN, 3 SPACE, 52 ESCAPE), `K <n> 0|1`,
`T <text>` (`\n` RETURN; a key every 6 frames), `B` BREAK.

## Layout

| Path | What |
|---|---|
| `main/main.c` | The Electron as an `emu_machine_t`: memory, start, status, console |
| `main/keymap.c` | The board's keyboards (HID usages) to the Electron's keys, by position |
| `main/rom_store.c` | The ROMs from their partition slots, or the SD card's `electron/roms` |
| `main/settings.c` | NVS `elk` |
| `components/electron/` | The machine, platform independent (`include/electron.h`) |
| `components/electron/elk.c` | Set up, reset, BREAK, the frame loop, the 6502's bus and its timing |
| `components/electron/ula.c` | The ULA's registers, interrupts, ROM paging |
| `components/electron/video.c` | The picture: line records, modes 0-6, three frames for the presenter |
| `components/electron/sound.c` | The square wave, two samples a line into the sound ring |
| `components/electron/keyboard.c` | The keyboard matrix as the CPU reads it |
| `components/electron/host/` | Host build: `make -C components/electron/host run` |
| `docs/electron-hardware.md` | The hardware research: registers, timing, tapes, the Plus 3, with sources |
| `roms/` | The ROMs (not in git; README.md says where they come from) |

## The machine

- **Time**: 2 MHz ticks, the 6502 core's cycles; a line 128 (64 us), a
  frame 312 lines (39936 ticks, 50.08 Hz). Line 0 is the picture's first.
- **Memory and timing** (`elk.c`): ROM reads at 2 MHz through the core's
  page pointers (a tick each); RAM and the ULA's registers through the bus
  callbacks, which wait for the 1 MHz clock (an even tick) and take two
  ticks; in modes 0-3 a RAM access in a picture line's first 80 ticks (the
  ULA's fetch) waits for its end (not in mode 3's two blank lines a row).
  $8000-$BFFF by the ROM page: 8-9 the keyboard, 10-11 BASIC, 4 the Plus
  3's ADFS, others $FF. $FC00-$FDFF the expansion bus (nothing yet: $FF),
  $FE00-$FEFF the ULA.
- **The ULA** (`ula.c`): interrupt status and enables ($FE00; the
  power-on bit cleared by its read), screen start ($FE02/$FE03: address
  bits 6-14, taken at the frame's start), ROM page and interrupt clears
  ($FE05: from pages 8-11 only to another of 8-15), counter ($FE06),
  control ($FE07: sound/cassette, mode, motor, CAPS LK), palette
  ($FE08-$FE0F). The display end interrupt as the picture ends (line 256,
  250 in modes 3 and 6), the 50 Hz one at line 99 (to be checked).
- **The picture** (`video.c`): BBC-style character rows (8 lines, 10 in
  modes 3 and 6), wrapping past $7FFF by the mode's screen size; each line
  recorded at its start (mode, palette registers, bytes), a finished frame
  that differs from the last shown handed to the presenter; 640 x 256.
- **The keyboard** (`keyboard.c`, `main/keymap.c`): 14 columns of 4 keys
  (MAME's electron.cpp has the layout); with ROM page 8 or 9 a read of
  $8000-$BFFF ORs the columns whose address line A0-A13 is low into bits
  0-3 (bits 4-7 0), at 1 MHz. A key's second symbol with SHIFT, the third
  with CTRL (RIGHT | \, LEFT ^ ~, UP £ {, DOWN _ }, COPY [ ]; ESCAPE then
  1-0 - LEFT RIGHT along the top). CAPS LK is SHIFT + FUNC (FUNC alone
  doesn't toggle it: checked on the host). BREAK isn't in the matrix: it
  holds the 6502 in reset and the ULA's power-on bit stays clear, so the
  MOS starts warm (no bell; `OLD` brings a program back: host). Typed text
  (`elk_type()`): a key held 3 frames, then 3 let go, letters by CAPS LK's
  light. The boards' keyboards by position: Tab and Alt FUNC, ` and Esc
  ESCAPE, = LEFT, [ ] UP DOWN, \ COPY; Backspace and Delete DELETE, the
  cursor keys the Electron's, End and Insert COPY, Caps Lock SHIFT + FUNC,
  F1-F10 FUNC + 1-9, 0 (f1-f9, f0), Pause and F12 BREAK, the keypad.
- **Sound** (`sound.c`): $FE07 bits 1-2 01: a square wave of 1 MHz / 16 /
  (counter + 1), averaged over half lines.
- Checked (2026-11-18): the host build boots to "Acorn Electron", "BASIC",
  ">" in 32 frames (the bell glyph after the name is the MOS's own: VDU 7
  and a bitmap at $C42B on a cold start, $DA3F); the MaTouch the same at
  100% (core 1 58%, the callbacks into PSRAM), with the flashing cursor;
  the Tab5 at 100% (core 1 29%). Typed BASIC from the console on both
  (`PRINT 6*7`, a FOR loop listed and RUN); every symbol typed on the host
  (`"\~[]{}_^@#$%&*()+-=;:,.<>/?|`).

## Lessons

## Testing

- Host: `make -C components/electron/host run` (300 frames of
  `roms/os_basic.ic2`): the BASIC prompt in `build/screen.ppm`. Switches in
  `host_main.c`: `ELK_LOG`, `ELK_SHOTS`, `ELK_STOP`, `ELK_TRACE`,
  `ELK_WATCH` (a CPU write to an address: the instructions before),
  `ELK_ZP` (zero page with the registers), `ELK_DASM`, `ELK_DUMP`,
  `ELK_KEYS`, `ELK_TYPE`, `ELK_WAV`, `ELK_ADFS`.

## Working with the user

- Expects changes built (both boards), flashed and checked on the board,
  then a short report of what was verified and what wasn't. Flashing ends
  the session on the board: say so first.
- Keep their private data (WiFi name, IP, SD card contents) out of anything
  meant for publishing (the setup screen's status lines: cut them out).
