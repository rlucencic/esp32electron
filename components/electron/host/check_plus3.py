"""Exercise the Acorn ADFS ROM with generated disk images."""
import os
from pathlib import Path
from subprocess import run
import sys
from tempfile import TemporaryDirectory

SECTOR = 256


def run_electron(executable, mos, adfs, path, commands, frames, create=False):
    env = {**os.environ, "ELK_ADFS": adfs, "ELK_TYPE": commands, "ELK_DUMP": "0D00:1"}
    env.pop("ELK_NEWDISK", None)
    env.pop("ELK_DISK0", None)
    env.pop("ELK_DISK1", None)
    env["ELK_NEWDISK" if create else "ELK_DISK0"] = str(path)
    return run(
        [executable, mos, str(frames), os.devnull],
        env=env, check=True, capture_output=True, text=True,
    ).stdout


def find_file(image, name):
    for offset in range(2 * SECTOR + 5, 2 * SECTOR + 5 + 47 * 26, 26):
        entry = image[offset:offset + 26]
        if entry[0] == 0:
            break
        text = bytes(v & 0x7F for v in entry[:10]).rstrip(b"\0\r ")
        if text == name:
            length = int.from_bytes(entry[18:22], "little")
            sector = int.from_bytes(entry[22:25], "little")
            return sector, length
    raise AssertionError(f"{name!r} is missing from the ADFS catalogue")


def checksum(sector):
    value = 255
    for byte in reversed(sector[:255]):
        value = (value & 255) + (value >> 8) + byte
    return value & 255


def main(executable, mos, adfs):
    with TemporaryDirectory() as folder:
        path = Path(folder) / "compact.adf"
        run_electron(
            executable, mos, adfs, path,
            r'85:*MOUNT 0\n|260:10 ?&D00=42\n|410:SAVE "FIRST"\n|620:SAVE "KEEP"\n',
            880, create=True,
        )
        image = path.read_bytes()
        assert find_file(image, b"FIRST")[0] == 7 and find_file(image, b"KEEP")[0] == 8
        run_electron(
            executable, mos, adfs, path,
            r'85:*MOUNT 0\n|260:*DELETE FIRST\n|490:*COMPACT\n|730:*CAT\n',
            1050,
        )
        image = path.read_bytes()
        sector, length = find_file(image, b"KEEP")
        assert sector == 7, "ADFS did not compact KEEP into the hole"
        program = image[sector * SECTOR:sector * SECTOR + length]
        result = run_electron(
            executable, mos, adfs, path,
            r'85:*MOUNT 0\n|260:LOAD "KEEP"\n|480:RUN\n',
            660,
        )
        assert "2A" in result.splitlines(), result

        # Leave free space only on side 1 so the original ADFS ROM has to
        # issue its side-select and restore sequence before saving data.
        start = 1280
        media = bytearray(640 * 1024)
        media[:7 * SECTOR] = image[:7 * SECTOR]
        media[0:3] = start.to_bytes(3, "little")
        media[0xFC:0xFF] = (2 * start).to_bytes(3, "little")
        media[0x100:0x103] = start.to_bytes(3, "little")
        media[0x1FE] = 3
        media[0xFF] = checksum(media)
        media[0x1FF] = checksum(media[SECTOR:])
        for ext, offset in (("adl", 16 * SECTOR), ("adf", start * SECTOR)):
            path = Path(folder) / f"second-side.{ext}"
            path.write_bytes(media)
            run_electron(
                executable, mos, adfs, path,
                r'85:*MOUNT 0\n|260:10 ?&D00=42\n|420:SAVE "SIDEONE"\n', 750,
            )
            image = path.read_bytes()
            assert find_file(image, b"SIDEONE")[0] == start
            assert image[offset:offset + length] == program, f"wrong side layout for .{ext}"
    print("Electron Plus 3 checks passed")


if __name__ == "__main__":
    main(*sys.argv[1:])
