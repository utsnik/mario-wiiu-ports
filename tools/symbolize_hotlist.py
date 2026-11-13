#!/usr/bin/env python3
"""Rewrite hotname6 raw caller fields in a Wii U log using the build ELF.

The console address is the ELF address plus the loaded text offset.  The
default is the offset used by the current SMS RPX; pass --text-offset when a
run has a different load address.
"""
import argparse
import bisect
import pathlib
import re
import subprocess
import sys

FIELD_RUN = re.compile(r"(?: c[0-7]=[0-9a-fA-F]{8}(?::[^\s]*)?)+")
FIELD = re.compile(r" c([0-7])=([0-9a-fA-F]{8})(?::[^\s]*)?")


def symbols(elf):
    root = pathlib.Path(elf).resolve().parent
    name = pathlib.Path(elf).name
    command = [
        "docker", "run", "--rm", "-v", f"{root}:/elf:ro", "soh-wiiu-builder:latest",
        "powerpc-eabi-nm", "-n", "-C", f"/elf/{name}",
    ]
    try:
        output = subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"cannot run nm in soh-wiiu-builder: {exc}")
    rows = []
    for line in output.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]) and parts[1] != "U":
            rows.append((int(parts[0], 16), parts[2]))
    rows.sort()
    return rows


def dropped(name):
    lower = name.lower()
    return (name.startswith(("J3D", "JDrama", "GX", "gx")) or
            "j3d" in lower or "jdrama" in lower or "platform" in lower or
            "sms_wiiu_" in lower or "gx2" in lower or "gx_stub" in lower or
            "whb" in lower or "port_" in lower or "perform" in lower)


def resolve(address, offset, rows, addresses):
    target = address - offset
    index = bisect.bisect_right(addresses, target) - 1
    if index < 0:
        return f"0x{address:08x}"
    return rows[index][1]


def rewrite(line, offset, rows, addresses):
    def replace(match):
        def field_replace(field):
            address = int(field.group(2), 16)
            name = resolve(address, offset, rows, addresses)
            return f" c{field.group(1)}={name}"
        return FIELD.sub(field_replace, match.group(0))
    return FIELD_RUN.sub(replace, line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=pathlib.Path)
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--text-offset", type=lambda value: int(value, 0), default=0x0A000000)
    args = parser.parse_args()
    rows = symbols(args.elf)
    addresses = [address for address, _ in rows]
    for line in args.log.read_text(errors="replace").splitlines(keepends=True):
        sys.stdout.write(rewrite(line, args.text_offset, rows, addresses))


if __name__ == "__main__":
    main()
