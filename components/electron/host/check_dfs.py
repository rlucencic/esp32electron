"""Exercise the original ACP DFS firmware with full and trimmed SSDs."""

import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def run_electron(executable, mos, dfs, disk, commands, frames, create=False):
    env = {key: value for key, value in os.environ.items() if not key.startswith("ELK_")}
    env.update(ELK_DFS=dfs, ELK_TYPE=commands, ELK_DUMP="0D00:1")
    env["ELK_NEWDISK" if create else "ELK_DISK0"] = str(disk)
    return subprocess.run(
        [executable, mos, str(frames), os.devnull],
        env=env, check=True, capture_output=True, text=True,
    ).stdout


def check_double_sided(executable, mos, dfs, folder):
    track_bytes = 10 * 256
    for tracks in (40, 80):
        disk = folder / f"double-{tracks}.dsd"
        capacity = (tracks * 10).to_bytes(2, "big")
        if tracks == 40:
            # Retain just the first side's first track and the second
            # catalogue. This exercises growth when saving to drive 2.
            raw = bytearray(track_bytes + 512)
            for offset in (0x106, track_bytes + 0x106):
                raw[offset:offset + 2] = capacity
            disk.write_bytes(raw)
        else:
            run_electron(executable, mos, dfs, disk, "", 0, create=True)
            raw = disk.read_bytes()
            assert len(raw) == 2 * tracks * track_bytes
            assert raw[0x106:0x108] == raw[track_bytes + 0x106:track_bytes + 0x108] == capacity

        run_electron(executable, mos, dfs, disk, r'100:10 ?&D00=42\n|300:SAVE "FRONT"\n', 620)
        front = disk.read_bytes()[:track_bytes]
        run_electron(executable, mos, dfs, disk,
                     r'100:*DRIVE 2\n|290:10 ?&D00=43\n|485:SAVE "BACK"\n', 750)
        saved = disk.read_bytes()
        assert saved[:track_bytes] == front
        assert saved[track_bytes + 8:track_bytes + 12] == b"BACK"
        if tracks == 40:
            assert len(saved) == track_bytes + 3 * 256

        for side, name, value in ((0, "FRONT", "2A"), (2, "BACK", "2B")):
            commands = rf'100:*DRIVE {side}\n|280:LOAD "{name}"\n|530:RUN\n'
            assert value in run_electron(executable, mos, dfs, disk, commands, 710).splitlines()


def main(executable, mos, dfs):
    with TemporaryDirectory() as directory:
        folder = Path(directory)
        disk = folder / "created.ssd"
        save = r'100:10 ?&D00=42\n|260:SAVE "DFSFILE"\n'
        reload = r'110:LOAD "DFSFILE"\n|390:RUN\n'
        run_electron(executable, mos, dfs, disk, save, 600, create=True)
        full = disk.read_bytes()
        assert len(full) == 204800
        assert full[8:15] == b"DFSFILE"
        assert full[0x105:0x108] == b"\x08\x03\x20"

        for tracks in (40, 80):
            # This image contains only the catalogue. The first SAVE must
            # append its data sector to the mounted file.
            disk = folder / f"trimmed-{tracks}.ssd"
            catalogue = bytearray(512)
            catalogue[0x106:0x108] = (tracks * 10).to_bytes(2, "big")
            disk.write_bytes(catalogue)
            run_electron(executable, mos, dfs, disk, save, 600)
            assert disk.stat().st_size == 768
            result = run_electron(executable, mos, dfs, disk, reload, 600)
            assert "2A" in result.splitlines(), result

            # Use the ROM's formatter on an unformatted, full-size target.
            disk = folder / f"format-{tracks}.ssd"
            disk.write_bytes(bytes(tracks * 10 * 256))
            run_electron(executable, mos, dfs, disk, rf'100:*FORM {tracks} 0\n|450:Y',
                         50 * tracks * 3)
            formatted = disk.read_bytes()
            assert formatted[0x105:0x108] == b"\0" + (tracks * 10).to_bytes(2, "big")
            assert formatted[-256:] == b"\xe5" * 256

        check_double_sided(executable, mos, dfs, folder)
    print("Electron ACP DFS checks passed")


if __name__ == "__main__":
    main(*sys.argv[1:])
