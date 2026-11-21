# Acorn Electron ROM images

Put the ROM images here (not part of this repository). `idf.py flash` writes
them into 64 KB slots of the `elk_roms` flash partition.

| File | What | Size | Needed for |
|---|---|---|---|
| `os_basic.ic2` | BBC BASIC II ($8000-$BFFF) and the Electron MOS 1.00 ($C000-$FFFF), as on the Electron's board | 32 KB (CRC32 b997f9cb) | the machine |
| `adfs.rom` | Acorn ADFS 1.0 for the Plus 3 | 16 KB (CRC32 3289bdc6) | Expansion: Plus 3 |
| `plus1.rom` | Acorn Expansion 1.00 for the Plus 1 | 4 KB (CRC32 ac30b0ed) | Optional cartridge boot and ROM filing system |
| `acpdfs.rom` | ACP Advanced Electron 1770 DFS 2.20 | 16 KB | ACP 1770 DFS disk expansion |

A file of the same name in the `electron/roms` folder on the SD card is used
instead of the flashed one.

The MOS/BASIC and ADFS images are in MAME's `electron` ROM set. The
Expansion 1.00 ROM is in its `plus1` device set and in the TOSEC Acorn
Electron archive as “Acorn Electron Expansion ROM v1.00 (1984)(Acorn)”.
For DFS, use the ACP Electron 2.20 dump and rename it to `acpdfs.rom`.
The Stairway to Hell archive's 2.20 image (CRC32 f3dc9bc5) was used for
testing. [Electron ROM archive](https://www.stairwaytohell.com/roms/homepage.html)

The MOS alone is
also in the TOSEC Acorn Electron set ("Acorn Electron OS (1983)(Acorn)":
the same code, dumps differing only in $FC00-$FEFF, the I/O pages the CPU
never reads it at); BASIC II is the BBC Micro's (CRC32 79434781). The ROMs
are Acorn's copyright.
