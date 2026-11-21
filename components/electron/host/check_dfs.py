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
    print("Electron ACP DFS checks passed")


if __name__ == "__main__":
    main(*sys.argv[1:])
