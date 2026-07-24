#!/usr/bin/env python3
"""Reject unsafe ELF32/MIPS kernel relocations and malformed HI/LO pairs."""

import struct
import sys


SHT_REL = 9
R_MIPS_HI16 = 5
R_MIPS_LO16 = 6
R_MIPS_GPREL16 = 7


def fail(path, message):
    print(f"check-mips-elf-relocs: {path}: {message}", file=sys.stderr)
    return 1


def check(path):
    data = open(path, "rb").read()
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1:
        return fail(path, "not an ELF32 object")
    if data[5] == 2:
        endian = ">"
    elif data[5] == 1:
        endian = "<"
    else:
        return fail(path, "unknown ELF byte order")

    e_shoff = struct.unpack_from(endian + "I", data, 32)[0]
    e_shentsize, e_shnum = struct.unpack_from(endian + "HH", data, 46)
    if e_shentsize < 40 or e_shoff + e_shentsize * e_shnum > len(data):
        return fail(path, "bad section table")

    sections = []
    for index in range(e_shnum):
        off = e_shoff + index * e_shentsize
        sections.append(struct.unpack_from(endian + "10I", data, off))

    errors = 0
    for relsec in sections:
        sh_type = relsec[1]
        sh_offset, sh_size, sh_info, sh_entsize = (
            relsec[4], relsec[5], relsec[7], relsec[9]
        )
        if sh_type != SHT_REL:
            continue
        if sh_info >= len(sections) or sh_entsize < 8 or sh_size % sh_entsize:
            errors += fail(path, "bad REL section")
            continue
        if sh_offset + sh_size > len(data):
            errors += fail(path, "truncated REL section")
            continue

        relocs = []
        for off in range(sh_offset, sh_offset + sh_size, sh_entsize):
            r_offset, r_info = struct.unpack_from(endian + "II", data, off)
            relocs.append((r_offset, r_info >> 8, r_info & 0xff))

        for index, (r_offset, symbol, r_type) in enumerate(relocs):
            if r_type == R_MIPS_GPREL16:
                errors += fail(
                    path,
                    f"GPREL16 at 0x{r_offset:x} requires an initialized $gp",
                )
                continue
            if r_type != R_MIPS_HI16:
                continue
            following = index + 1
            while following < len(relocs) and relocs[following][2] == R_MIPS_HI16:
                if relocs[following][1] != symbol:
                    break
                following += 1
            if (following >= len(relocs) or
                    relocs[following][2] != R_MIPS_LO16 or
                    relocs[following][1] != symbol):
                errors += fail(
                    path,
                    f"HI16 at 0x{r_offset:x} has no adjacent matching LO16",
                )
    return errors


def main():
    if len(sys.argv) < 2:
        print("usage: check-mips-elf-relocs.py object...", file=sys.stderr)
        return 2
    errors = sum(check(path) for path in sys.argv[1:])
    if errors:
        print(f"check-mips-elf-relocs: {errors} error(s)", file=sys.stderr)
        return 1
    print(f"check-mips-elf-relocs: {len(sys.argv) - 1} object(s), ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
