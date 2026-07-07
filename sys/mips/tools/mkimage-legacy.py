#!/usr/bin/env python3
import argparse
import os
import struct
import time
import zlib


MAGIC = 0x27051956

OS_CODES = {
    "linux": 5,
}

ARCH_CODES = {
    "mips": 5,
}

TYPE_CODES = {
    "kernel": 2,
}

COMP_CODES = {
    "none": 0,
}


def parse_u32(text):
    return int(text, 0) & 0xffffffff


def parse_args():
    parser = argparse.ArgumentParser(
        description="Create a U-Boot legacy uImage header."
    )
    parser.add_argument("-A", dest="arch", required=True, choices=ARCH_CODES)
    parser.add_argument("-O", dest="os_name", required=True,
                        choices=OS_CODES)
    parser.add_argument("-T", dest="image_type", required=True,
                        choices=TYPE_CODES)
    parser.add_argument("-C", dest="compression", required=True,
                        choices=COMP_CODES)
    parser.add_argument("-a", dest="load", required=True, type=parse_u32)
    parser.add_argument("-e", dest="entry", required=True, type=parse_u32)
    parser.add_argument("-n", dest="name", required=True)
    parser.add_argument("-d", dest="data", required=True)
    parser.add_argument("output")
    return parser.parse_args()


def image_time(path):
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"]) & 0xffffffff
    return int(os.path.getmtime(path) if os.path.exists(path) else time.time())


def main():
    args = parse_args()
    with open(args.data, "rb") as input_file:
        payload = input_file.read()

    name = args.name.encode("ascii")
    if len(name) > 32:
        raise SystemExit("uImage name is longer than 32 bytes")
    name = name + b"\0" * (32 - len(name))

    data_crc = zlib.crc32(payload) & 0xffffffff
    timestamp = image_time(args.data)
    header = struct.pack(
        ">7I4B32s",
        MAGIC,
        0,
        timestamp,
        len(payload),
        args.load,
        args.entry,
        data_crc,
        OS_CODES[args.os_name],
        ARCH_CODES[args.arch],
        TYPE_CODES[args.image_type],
        COMP_CODES[args.compression],
        name,
    )
    header_crc = zlib.crc32(header) & 0xffffffff
    header = struct.pack(
        ">7I4B32s",
        MAGIC,
        header_crc,
        timestamp,
        len(payload),
        args.load,
        args.entry,
        data_crc,
        OS_CODES[args.os_name],
        ARCH_CODES[args.arch],
        TYPE_CODES[args.image_type],
        COMP_CODES[args.compression],
        name,
    )

    with open(args.output, "wb") as output_file:
        output_file.write(header)
        output_file.write(payload)


if __name__ == "__main__":
    main()
