# esp32electron: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **The ULA from the research notes** (`docs/electron-hardware.md`): the
   palette registers' bit layout, the interrupts' lines (the 50 Hz one,
   display end), what the other registers read, BREAK vs power-on, the
   contention's exact rules; checked against the MOS's own code.
2. **The keyboard by hand**: done but for a person typing (CLAUDE.md, the
   keyboard): a Bluetooth keyboard on the Tab5 (FUNC keys, CAPS LK,
   COPY editing, BREAK, ESCAPE), the Tab5 Keyboard.
3. **Speed**: core 1 58% on the MaTouch at the prompt (every RAM access a
   callback into PSRAM); profile, then RAM in faster memory or the 6502
   core's fast path with the 1 MHz cost.
4. **Tapes**: UEF images (gzip too) through the ULA's cassette interface
   (high tone, bytes, the interrupts), with the motor; *CAT, LOAD, CHAIN,
   *RUN; the archive.org TOSEC games (~/Development/electron-dl), a fast
   load; drives on the setup screen and web page.
5. **The Plus 3**: the WD1770 and its latch at $FCC0, the ADFS ROM in page
   4, 3.5" ADFS images (.adf/.adl): *CAT, LOAD, SAVE, *FORM80/*COMPACT,
   kept after remounting (esp32emu's plan: required).
6. **Original software**: the games and applications from the TOSEC set;
   ROM cartridges (the Plus 1); the SSD images through ACP's 1770 DFS.
7. **GitHub**: the repository public with screenshots and topics, when the
   user wants it.
