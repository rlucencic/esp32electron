# esp32electron: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

The [original software test notes](CLAUDE.md#testing) record the completed
archive pass and the host and board workflows it covered.

1. **Keep a small replay set from the completed archive pass.** Record local
   fixture names, hashes, input steps and observable checkpoints for a
   multi-stage tape, a split Plus 1 cartridge and a disk application that saves
   and reloads. Use the five `components/electron/host/Makefile` check targets
   as the entry points. Keep the original software archives outside Git.

2. **Check media-switch failure recovery.** Use the UEF reader in
   `components/electron/uef.c` and existing host check scripts to cover a
   truncated chunk, a bad gzip CRC and a valid image after failure. Record the
   UI and guest drive state when replacement fails. Repeat on the MaTouch with
   a large valid UEF while Wi-Fi is active.

3. **Verify recording persistence across a restart.** Save to `.rec.uef`, pause
   and restart the board, then load the same program from the remembered media.
   Also check eject/remount and fast-leader versus real-time playback, using
   one scratch recording per board.
