# esp32electron: an Acorn Electron on the MaTouch ESP32-S3 7" and the M5Stack Tab5

An Acorn Electron (1983): a 6502A at 2 MHz (1 MHz to RAM and the ULA), 32
KB RAM, the ULA (display, sound, cassette, keyboard, interrupts, ROM
paging), BBC BASIC II and MOS 1.00 in a 32 KB ROM, an Acorn Plus 3
(ADFS and a WD1770 for 3.5" drives), and the Plus 1 cartridge sockets.
Both boards, as the QL. The plan: esp32emu's
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
  `os_basic.ic2` slot 0 (32 KB: BASIC then the MOS), `adfs.rom` slot 1,
  optional `plus1.rom` slot 2 and optional `acpdfs.rom` slot 3. A
  file of the same name in the SD card's `electron/roms` overrides one
  (`roms/README.md`: where they come from). The boards are shared with the
  other projects: after another one, a full `flash`.
- NVS: namespace `elk` (`disk_expansion`, `fast_tape`), mounted tapes and disks in `elkdisks`. mDNS
  `acorn-electron.local`.
- The SD card's `electron/` folder: UEF tapes (`.uef`, gzip-compressed
  ones too, the usual kind: TOSEC's are), listed by the web page and the
  setup screen's Media page. Playback uses the ULA cassette receiver.
  The same folder holds writable ADFS and DFS disks for the two floppy drives.
  Plus 1 sockets SK1 and SK2 take read-only `.rom` and `.bin` images.
- The MaTouch: its UART port, not native USB (the speaker's lines); opening
  that port resets the board.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | send "cmd" |
  info` with the IDF venv's Python (`-I`). Start on the setup screen: `t
  1065 637` (Tab5), `t 850 532` (MaTouch).

Console commands (`main/main.c`): `k <n>` tap matrix key n (`electron.h`'s
`ELK_KEY_*`: column * 4 + row; 6 RETURN, 3 SPACE, 52 ESCAPE), `K <n> 0|1`,
`T <text>` (`\n` RETURN; a key every 6 frames; the console takes lines of
about 128 characters: longer ones are cut, so type programs a line at a
time), `B` BREAK, `W` rewind tape.

The web page (esp32emu's, `http://acorn-electron.local`): the machine card
(`status()`: the system, the ROM at $8000 now, the mode (size, colours,
screen memory, start, whether the CPU waits), the tone's pitch, CAPS LK and
the keys' places, the cassette motor, the expansions), the drives card (the
Tape drive and two floppy drives), the board card, and the media files in
`electron/` with upload (ZIP files unpacked in the browser), download,
delete and insert. The Tape drive takes UEF, including gzip-compressed
images. The floppy drives take raw ADFS images and can create formatted
blank disks. Mounted media names are remembered across restarts.

## Layout

| Path | What |
|---|---|
| `main/main.c` | The Electron as an `emu_machine_t`: memory, start, the Tape drive (`disks`), status, console |
| `main/keymap.c` | The board's keyboards (HID usages) to the Electron's keys, by position |
| `main/rom_store.c` | The ROMs from their partition slots, or the SD card's `electron/roms` |
| `main/settings.c` | NVS `elk` |
| `components/electron/` | The machine, platform independent (`include/electron.h`) |
| `components/electron/elk.c` | Set up, reset, BREAK, the frame loop, the 6502's bus and its timing |
| `components/electron/ula.c` | The ULA's registers, interrupts, ROM paging |
| `components/electron/tape.c` | UEF cassette playback, transmit timing and append-only recording |
| `components/electron/uef.c` | Shared raw and gzip UEF reader with chunk validation |
| `components/electron/plus3.c` | WD1770, drive latch, raw ADFS and DFS images and disk writeback |
| `components/electron/plus1.c` | Raw ROM cartridge loading and mapping for SK1 and SK2 |
| `components/electron/video.c` | The picture: line records, modes 0-6, three frames for the presenter |
| `components/electron/sound.c` | The square wave, two samples a line into the sound ring |
| `components/electron/keyboard.c` | The keyboard matrix as the CPU reads it |
| `components/electron/host/` | Host build: `make -C components/electron/host run` |
| `docs/electron-hardware.md` | The hardware research: registers, timing, tapes, the Plus 3, with sources |
| `roms/` | The ROMs (not in git; README.md says where they come from) |

## The machine

- **Time**: 2 MHz ticks, the 6502 core's cycles; a line 128 (64 us),
  tick 0 its first pixel; interlaced fields of 312 and 313 lines in turn
  (39936 and 40064 ticks: 50 Hz). Line 0 is the picture's first.
- **Memory and timing** (`elk.c`; docs 4.4): ROM reads at 2 MHz through
  the core's page pointers (a tick each); RAM in modes 4-6 below the
  screen through them too, as the core's slow pages
  (`CONFIG_M6502_SLOW_PAGES`: the next even tick, two ticks; the screen's
  pages through the callbacks, which notice what changes the picture);
  RAM in modes 0-3, the ULA's registers, the keyboard and $FC00-$FDFF
  through the bus callbacks at 1 MHz (`elk_map_ram()` on each mode
  change): an access
  takes the next slot starting on an even tick (2 ticks from an even start,
  3 from an odd one); in modes 0-3 a RAM slot in a pixel line's first 80
  ticks is the ULA's and the access waits for the one at tick 80 (pixel
  lines: 0-255, in mode 3 lines 0-7 of each 10-line row). The BASIC loop
  `TIME=0:FOR A=0 TO 10000:NEXT` takes 14.71 / 14.74 / 14.89 / 11.94 /
  7.07 / 7.08 / 7.07 s in modes 0-6 on the host, two real Electrons 14.71
  / 14.74 / 14.89-14.90 / 11.92-11.94 / 7.07-7.08 (docs 4.5); mode 0 on
  the boards 14.72 (Tab5) and 14.70 s (MaTouch), at 100% (core 1 23% and
  64%). $8000-$BFFF
  by the ROM page: 8-9 the keyboard, 10-11 BASIC, 4 the Plus 3's ADFS,
  others $FF. $FC00-$FDFF the expansion bus (nothing yet: $FF), $FE00-$FEFF
  the ULA.
- **The ULA** (`ula.c`): interrupt status and enables ($FE00; the
  power-on bit cleared by its read), screen start ($FE02/$FE03: address
  bits 6-14, taken at the frame's start), ROM page and interrupt clears
  ($FE05: from pages 8-11 only to another of 8-15), counter ($FE06),
  control ($FE07: sound/cassette, mode, motor, CAPS LK), palette
  ($FE08-$FE0F); enables bits 6-2 only; mode 7 is mode 4. Display end at
  tick 96 (the HS) of the picture's last line (255, 249 in modes 3 and 6);
  the 50 Hz interrupt at line 99, tick -2 in the 312-line field and 62 in
  the 313-line one (docs 2.5: the User Guide's scope measurements).
- **The picture** (`video.c`): BBC-style character rows (8 lines, 10 in
  modes 3 and 6); the 15-bit address counter coming to $0000-$07FF (past
  $7FFF, or a start that low) goes on at the mode's base plus its low 11
  bits; each line recorded at its start (mode, palette registers, bytes), a
  finished frame that differs from the last shown handed to the presenter
  (compared only after a change: a RAM write of a new value at the mode's
  screen base or above, a palette, mode or start register given a new
  value; the MOS rewrites its cursor's byte every field, BASIC's stack is
  just under the screen);
  640 x 256. The palette (docs 3.4): a pixel's 4-bit index (2 colours 0/8,
  4 colours 0/2/8/10, 16 all) to 8 colours, a bit a gun, 1 = off; each
  register pair holds four indices (10 8 2 0, 14 12 6 4, 15 13 7 5, 11 9 3
  1). Checked: mode 2's 16 colours, 5's and 0's as the MOS sets them
  (host; mode 2 on the Tab5 too: a screenshot over USB is slow enough to
  catch the flashing colours 8-15 in both phases, in bands).
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
  By hand: the Tab5 Keyboard works (the user, 2026-11-18). Not checked by
  hand yet (as with the QL: the user reports what fails): a Bluetooth or
  USB keyboard's FUNC keys, CAPS LK, COPY editing, BREAK, ESCAPE.
- **Sound** (`sound.c`): $FE07 bits 1-2 01: a square wave of 1 MHz / 32 /
  (counter + 1) (docs 5: the MOS's lowest note is B2, 123.5 Hz), averaged
  over half lines.
- Checked (2026-11-18): the host build boots to "Acorn Electron", "BASIC",
  ">" in 32 frames (the bell glyph after the name is the MOS's own: VDU 7
  and a bitmap at $C42B on a cold start, $DA3F); the MaTouch the same at
  100% (core 1 58%, the callbacks into PSRAM), with the flashing cursor;
  the Tab5 at 100% (core 1 29%). Typed BASIC from the console on both
  (`PRINT 6*7`, a FOR loop listed and RUN); every symbol typed on the host
  (`"\~[]{}_^@#$%&*()+-=;:,.<>/?|`).

## Lessons

- **Watch what is written, not that it is**: a "picture changed" flag set
  by any RAM write at $3000 or above (BASIC's stack under the screen), by
  any write to $FE07 or the palette (the MOS rewrites them with the same
  values) and by the cursor routine's byte (rewritten every field, $C9C5)
  made the frame comparison run every field: 5% of the MaTouch's core 1,
  in the ROM's `memcmp` (`?40056000` in the profile: the S3 ROM's ELF has
  no sizes; sampling raw PCs found it). Only new values at the mode's
  screen base or above count now.
- **The web page showed only its title** (2026-11-18): without a `disks`
  config `/api/info`'s disks were nulls, and the page's `accepted()` threw
  on the null description before it asked for the status, so every card
  stayed empty. The page copes with that now (esp32emu; any board without
  an SD card had the same), and the Electron has its Tape drive and folder.
  Checked on the MaTouch (2026-11-18): `/api/info`, `/api/status`,
  `/api/disks`, the served page run under node against them (every card
  filled), a TOSEC UEF uploaded, downloaded identical, inserted (a text
  file named `.uef` refused with 422), kept across a reset, the light on
  with `*MOTOR 1`, deleted only once ejected.
- **Speed on the MaTouch** (2026-11-18, core 1 busy): the BASIC prompt 64%
  -> 57% (the comparison) -> 44% (slow pages), the mode 4 loop 81% -> 71%,
  the mode 1 loop (contended: RAM still through callbacks) 69% -> 61%
  (its line copy 9.7% -> 5.3%, without the wrap test inside a line).

## Testing

- **Original tape and disk software (2026-11-22).** Another 24 TOSEC
  tapes reached their menus, opening screens or gameplay on the host.
  Both Real time and Fast leaders completed the later loading stages
  of Galaforce 2, Hostages, Imogen, Perplexity, Predator and Sim City.
  Fast leaders saved 31 to 52 seconds across those six tapes. Sim City
  still took about six minutes and received the same 42,078 bytes in
  both modes.
  Thrust reached its first level, Podd acted out `jump`, Castle of
  Riddles accepted `LOOK` and `NORTH`, and Acornsoft Chess answered
  `E2E4` with `Ng8-f6`. Sim City generated a city map, Acornsoft
  Forth evaluated `7 6 * .`, and Turtle Graphics drew a right angle.
  Repton Infinity booted from its program SSD. In Play Game, `L` opened
  the filename prompt. Swapping to the data SSD in the same drive and
  entering `Rep4A` loaded level 1.
  The MaTouch also completed the Repton Infinity disk swap and entered
  the first level of Thrust from its original SSD.
  The Tab5 read all 21,169 bytes of the original Podd tape and acted
  out `jump`.
- **Double-sided DFS (2026-11-21).** ACP DFS uses drives 0 and 2 for
  the two surfaces of the first disk and 1 and 3 for the second disk.
  The host played Snapper and Repton from opposite sides of one DSD
  assembled from the supplied SSD images. Repton also booted from the
  second side of a gzip UEF. The original ROM formatted both sides of
  a 40-track DSD and verified the second side. `check-dfs` tests
  independent catalogues and save/reload on 40- and 80-track DSDs.
  ACP's `*BACKUP 0 2` copied every track between sides. The MaTouch and
  Tab5 booted Repton from drive 2, saved and ran a BASIC program on drive 3
  after remounting, and switched back to ADFS to catalogue the Welcome disk.
- **Cassette recording (2026-11-21).** The ULA raises TxEmpty at the
  end of the eighth data bit. The host recorded a BASIC program through
  the MOS, then appended an 18-block machine-code file in a fresh process.
  Both loaded from the saved UEF, and the block data matched byte for
  byte. The host also checked that creation fails if a file already
  exists, and an imported tape remains unchanged after a guest SAVE.
  Exile saved a four-block position file through its own menu. The MOS
  loaded the recorded 1,024 bytes back and matched the original data.
  Both the MaTouch and Tab5 created a recording tape over the web
  interface, saved a BASIC program to SD, then remounted and ran it.
  The MaTouch appended another 18 blocks without changing the first
  recording, and the host reloaded all 4,608 appended bytes.
- **Original software and UEF disk media (2026-11-21).** Twelve additional
  original tapes reached their menus or opening screens, including all
  four Mini Office programs, Exile, Citadel and Magic Mushrooms. Seven
  `.uef` files in the archive turned out to contain trimmed ADFS disks.
  Mount them in a floppy drive. All seven booted via `*EXEC !BOOT` on the
  host. The new `check-uef-disk` test covers raw and gzip UEFs containing
  ADFS and DFS disk data, and rejection of disk-only UEFs in the tape drive.
  Palace of Magic loaded its second stage and reached gameplay. A
  synthetic two-sided ADFS UEF also loaded a program from the second side.
  ViewSheet saved a spreadsheet to ADFS and reopened it in a fresh
  host session with the value intact.
  The MaTouch booted Bug Blaster from its UEF disk and loaded Mini Office
  Graphics from cassette. The Tab5 booted Palace of Magic and switched
  between ACP DFS and Acorn Plus 3 from the mounted UEF disk type.
  Both boards rejected writes and preserved the original UEF bytes.
- **ACP 1770 DFS (2026-11-21).** The original ACP DFS 2.20 ROM runs
  from SK1, with 4 KiB of workspace and single-density WD1770 transfers.
  It shares the floppy drive slots with the Acorn Plus 3. Setup and the
  media browser select the interface. The host catalogued all 117 SSD
  images in the supplied TOSEC archive and reached the Snapper title
  via its `!BOOT` file. Chuckie Egg, Repton, Paperboy and Commando also
  started from their SSDs. `check-dfs` checks save and reload on trimmed
  SSDs and uses the ROM's `*FORM` command on 40- and 80-track images.
  The ROM's `*VERIFY` read all 80 tracks and `*BACKUP` produced a
  byte-for-byte copy on the second drive.
  Both boards booted Snapper from the trimmed SSD and reloaded a BASIC
  program after remounting a new disk (40 tracks on MaTouch, 80 on Tab5).
  Both then switched to Acorn Plus 3 and catalogued the ADFS Welcome disc.
- **Plus 1 cartridges (2026-11-21).** The host's `check-plus1` target
  verifies both banks, 8 KiB mirroring, and the ULA's priority over external
  ROMs. The original Expansion 1.00 booted VIEW from either socket on the
  host, including with ADFS active. VIEW saved a document to ADFS on the
  host. The MaTouch booted VIEW in SK1 and the Tab5 booted it in SK2.
  Both boards reopened a document after saving it and remounting its
  disk. The host also loaded the original
  Elite, Starship Command, Hopper, Sphinx Adventure and Arcadians tapes.
  Elite reached the Commander Jameson status screen, Starship Command,
  Hopper and Arcadians reached gameplay, and Sphinx Adventure accepted
  movement and look commands.
- **Acorn Plus 3 (2026-11-21).** The original ADFS ROM uses page 4 and
  polls the WD1770 at $FCC4–$FCC7. The latch at $FCC0 selects the drive,
  side, density and controller reset. Setup enables the expansion and
  the media browser can create formatted S, M and L disks. The host
  catalogued the original Welcome disc and ran its `*EFORM` utility to
  format and verify all three geometries. The host's `check-plus3` target
  checks saving, moving a file with `*COMPACT`, reloading after remount,
  and second-side writes in both raw image layouts.
  The MaTouch and Tab5 catalogued the Welcome disc, saved a BASIC program
  to a new disk, and reloaded and ran it after ejecting and remounting
  (320 KiB on MaTouch, 640 KiB on Tab5). The MaTouch uses esp32emu's
  reserved SD DMA buffer and PSRAM web transfer buffers.
- **UEF cassette playback (2026-11-21).** The receiver uses $FE04, the
  motor and input mode in $FE07, and the RxFull and high-tone interrupts.
  The player runs at nominal 1200 baud with a bit clock in the Electron's
  2 MHz timebase. The Fast leaders setting limits long carriers and gaps
  while preserving the data rate. On the host, Chuckie Egg reached its
  title after about 127 seconds in Real time and 115 with Fast leaders.
  Snapper and Repton reached their menus. All 869 cassette UEFs in the
  supplied TOSEC archive mounted in the host tape drive. Generated
  tapes cover `LOAD`, `CHAIN`, `*RUN`, explicit bits and gzip validation
  in `make -C components/electron/host check`. The MaTouch and Tab5
  catalogued and ran a BASIC program from a gzip UEF uploaded through
  the web interface. The MaTouch also played the original TOSEC Snapper
  image to its menu in Real time.
- Host: `make -C components/electron/host run` (300 frames of
  `roms/os_basic.ic2`): the BASIC prompt in `build/screen.ppm`. Switches in
  `host_main.c`: `ELK_LOG`, `ELK_SHOTS`, `ELK_STOP`, `ELK_TRACE`,
  `ELK_WATCH` (a CPU write to an address: the instructions before),
  `ELK_ZP` (zero page with the registers), `ELK_DASM`, `ELK_DUMP`,
  `ELK_KEYS`, `ELK_TYPE`, `ELK_WAV`, `ELK_ADFS`, `ELK_TAPE`, and
  `ELK_TAPE_FAST`. `ELK_NEWTAPE` creates and mounts a blank
  `.rec.uef`; use `ELK_TAPE` to append to one already on disk.
  For the Plus 3, set `ELK_ADFS` and mount raw images
  with `ELK_DISK0` and `ELK_DISK1`. `ELK_NEWDISK` creates a formatted
  ADFS M image and mounts it in drive 0. For the Plus 1, use `ELK_PLUS1`
  for Expansion 1.00 and `ELK_SK1` and `ELK_SK2` for the cartridges.
  Set `ELK_DFS` in place of `ELK_ADFS` for ACP DFS. In DFS mode,
  `ELK_NEWDISK` creates a formatted 80-track SSD or DSD according to
  the filename extension.

## Working with the user

- Expects changes built (both boards), flashed and checked on the board,
  then a short report of what was verified and what wasn't. Flashing ends
  the session on the board: say so first.
- Keep their private data (WiFi name, IP, SD card contents) out of anything
  meant for publishing (the setup screen's status lines: cut them out).
