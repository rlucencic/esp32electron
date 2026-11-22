"""Check Plus 1 paging through the Electron CPU bus."""

import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def main(executable, mos):
    with TemporaryDirectory() as directory:
        folder = Path(directory)
        sk1, sk2 = folder / "sk1.rom", folder / "sk2.rom"
        firmware = folder / "plus1.rom"
        sk1.write_bytes(bytes([0x31]) * 16384 + bytes([0x42]) * 16384)
        sk2.write_bytes(bytes([0x53]) * 8192)
        firmware.write_bytes(bytes([0x64]) * 4096)

        # Change banks from RAM, where execution can continue across a
        # page switch. Store each sampled byte at &D00 for the host.
        instructions = bytearray([0x78])      # SEI

        def select(page, register=0xFE05):
            instructions.extend([0xA9, page, 0x8D, register & 0xFF, register >> 8])

        def sample(address, index):
            instructions.extend([0xAD, address & 0xFF, address >> 8, 0x8D, index, 0x0D])

        select(2)                             # BASIC still drives the bus
        sample(0x8000, 0)
        select(12)
        sample(0xB000, 1)                     # mirrored expansion firmware
        select(2)
        sample(0x8000, 2)
        select(0x13)                          # interrupt acknowledge
        sample(0x8000, 3)
        select(3, 0xFE15)                     # only the ULA decodes the mirror
        sample(0x8000, 4)
        select(3)
        sample(0x8000, 5)
        select(0)
        sample(0xA000, 6)                     # mirrored 8 KiB cartridge
        select(1)
        sample(0x8000, 7)
        select(10)
        instructions.extend([0x58, 0x60])      # CLI, RTS

        lines = [f"10 FOR I%=0 TO {len(instructions) - 1}:READ B%:?(I%+&900)=B%:NEXT:CALL &900"]
        for line, offset in enumerate(range(0, len(instructions), 12), 2):
            values = ",".join(str(value) for value in instructions[offset:offset + 12])
            lines.append(f"{line * 10} DATA {values}")
        lines.append("RUN")
        schedule = "|".join(f"{100 + 380 * index}:{line}\\n" for index, line in enumerate(lines))
        env = {**os.environ, "ELK_PLUS1": str(firmware), "ELK_SK2": str(sk2),
               "ELK_TYPE": schedule, "ELK_DUMP": "0D00:8"}
        low = folder / "Test [lorom][ID].rom"
        high = folder / "Test [uprom][ID].rom"
        low.write_bytes(bytes([0x31]) * 16384)
        for invalid in (None, bytes(16383), bytes(16385)):
            if invalid is not None:
                high.write_bytes(invalid)
            result = subprocess.run([executable, mos, "0", os.devnull],
                                    env={**env, "ELK_SK1": str(low)}, capture_output=True)
            assert result.returncode == 1 and b"invalid or unreadable cartridge" in result.stderr
        high.write_bytes(bytes([0x42]) * 16384)

        expected = bytes([Path(mos).read_bytes()[0], 0x64, 0x31, 0x31, 0x31, 0x42, 0x53, 0xFF])
        for cartridge in (sk1, low, high):
            result = subprocess.run(
                [executable, mos, str(100 + 380 * len(lines)), os.devnull],
                env={**env, "ELK_SK1": str(cartridge)}, check=True, capture_output=True, text=True,
            )
            assert expected.hex(" ").upper() in result.stdout.splitlines(), result.stdout
    print("Electron Plus 1 paging checks passed")


if __name__ == "__main__":
    main(*sys.argv[1:])
