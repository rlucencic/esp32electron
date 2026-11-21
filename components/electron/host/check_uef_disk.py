"""Exercise implicit disk sides in raw and compressed UEF files."""

import gzip
import os
from pathlib import Path
from struct import pack
import subprocess
import sys
from tempfile import TemporaryDirectory


def chunk(kind, data):
    return pack("<HI", kind, len(data)) + data


def main(executable, mos, adfs, dfs):
    with TemporaryDirectory() as directory:
        folder = Path(directory)
        clean_env = {key: value for key, value in os.environ.items() if not key.startswith("ELK_")}
        test = [executable, mos, "950", os.devnull]
        wrapped = folder / "wrapped.uef"
        for system, firmware, extension, sectors in (
            ("ELK_ADFS", adfs, "adf", 16),
            ("ELK_DFS", dfs, "ssd", 10),
        ):
            disk = folder / f"original.{extension}"
            env = {**clean_env, system: firmware}
            mount = r"100:*MOUNT 0\n|" if sectors == 16 else ""
            save = mount + r'300:10 ?&D00=42\n|480:SAVE "SOURCE"\n'
            subprocess.run(test, env={**env, "ELK_NEWDISK": str(disk), "ELK_TYPE": save},
                           check=True, capture_output=True)
            # Only the occupied prefix is present, and the old disc-info
            # chunk omits the track count and filesystem recommendation.
            image = b"UEF File!\0\x04\0" + chunk(0x200, bytes([0, 0, 1, sectors, 0, 0]))
            image += chunk(0x201, b"\0" + disk.read_bytes()[:4096])
            for payload in (image, gzip.compress(image)):
                wrapped.write_bytes(payload)
                load = mount + r'310:LOAD "SOURCE"\n|550:RUN\n|690:SAVE "BLOCKED"\n'
                result = subprocess.run(
                    test, env={**env, "ELK_DISK0": str(wrapped), "ELK_TYPE": load, "ELK_DUMP": "0D00:1"},
                    check=True, capture_output=True, text=True,
                )
                assert "2A" in result.stdout.splitlines(), result.stdout
                assert wrapped.read_bytes() == payload
                rejected = subprocess.run(test, env={**env, "ELK_TAPE": str(wrapped)}, capture_output=True)
                assert rejected.returncode == 1, "disc-only UEF was accepted as tape"
    print("Electron UEF disk checks passed")


if __name__ == "__main__":
    main(*sys.argv[1:])
