"""Exercise the MOS cassette commands using generated tape images."""
from binascii import crc_hqx
import gzip
import os
from pathlib import Path
from struct import pack
from subprocess import run
import sys
from tempfile import TemporaryDirectory


def chunk(kind, data):
    return pack("<HI", kind, len(data)) + data


def block(name, address, data):
    header = name + b"\0" + pack("<IIHHBI", address, address, 0, len(data), 0x80, 0)
    return (
        b"\x2a" + header + pack(">H", crc_hqx(header, 0))
        + data + pack(">H", crc_hqx(data, 0))
    )


def explicit_bits(data):
    bits = [bit for byte in data for bit in (0, *((byte >> n) & 1 for n in range(8)), 1)]
    unused = -len(bits) % 8
    bits.extend([0] * unused)
    return bytes([unused] + [
        sum(bits[i + n] << n for n in range(8)) for i in range(0, len(bits), 8)
    ])


def main(executable, rom):
    # BASIC line 10 ?&D00=66, and a machine code routine that writes $37 to &D01.
    basic = bytes.fromhex("0D 00 0A 0D 20 3F 26 44 30 30 3D 36 36 0D FF")
    code = bytes.fromhex("A9 37 8D 01 0D 60")
    leader = chunk(0x110, pack("<H", 3600))
    tape = b"UEF File!\0\x0A\0" + chunk(0x112, pack("<H", 2400))
    tape += leader + chunk(0x100, block(b"BASICTEST", 0xFFFF0E00, basic))
    tape += leader + chunk(0x102, explicit_bits(block(b"CODETEST", 0xFFFF2800, code)))
    expected_program = basic.hex(" ").upper()

    with TemporaryDirectory() as folder:
        path = Path(folder) / "check.uef"
        for payload, fast in ((tape, False), (gzip.compress(tape), True)):
            path.write_bytes(payload)
            env = {**os.environ, "ELK_TAPE": str(path), "ELK_DUMP": "0D00:2;0E00:15;2800:6"}
            env.pop("ELK_TAPE_FAST", None)
            if fast:
                env["ELK_TAPE_FAST"] = "1"
            for command, expected in (
                ('LOAD "BASICTEST"', expected_program),
                ('CHAIN "BASICTEST"', "42 00"),
                ("*RUN CODETEST", "40 37"),
            ):
                env["ELK_TYPE"] = rf"45:{command}\n"
                result = run([executable, rom, "450", os.devnull], env=env, check=True,
                             capture_output=True, text=True)
                if expected not in result.stdout.splitlines():
                    raise RuntimeError(f"{command} failed (fast={fast}):\n{result.stdout}")

        path.write_bytes(payload[:-1] + bytes([payload[-1] ^ 0x80]))
        result = run([executable, rom, "0", os.devnull], env=env, capture_output=True)
        if result.returncode != 1:
            raise RuntimeError("Corrupt gzip was accepted")
    print("Electron tape checks passed")


if __name__ == "__main__":
    main(*sys.argv[1:])
