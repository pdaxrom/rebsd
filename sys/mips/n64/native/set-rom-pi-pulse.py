#!/usr/bin/env python3
"""Set and verify the PI DOM1 pulse-width byte in a native Z64 image."""

import argparse
from pathlib import Path


def parse_byte(value: str) -> int:
    try:
        number = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid byte value: {value}") from error
    if number < 0 or number > 0xFF:
        raise argparse.ArgumentTypeError(f"byte value out of range: {value}")
    return number


def set_pi_pulse(path: Path, pulse: int) -> None:
    with path.open("r+b") as rom:
        header = rom.read(4)
        if len(header) != 4:
            raise SystemExit(f"{path}: image is too short for an N64 header")
        if header[0] != 0x80 or header[1] != 0x37 or header[3] != 0x40:
            raise SystemExit(
                f"{path}: expected native Z64 PI header, found {header.hex()}"
            )

        old_pulse = header[2]
        rom.seek(2)
        rom.write(bytes((pulse,)))
        rom.flush()
        rom.seek(0)
        verified = rom.read(4)

    expected = bytes((0x80, 0x37, pulse, 0x40))
    if verified != expected:
        raise SystemExit(
            f"{path}: PI header verification failed: "
            f"expected {expected.hex()}, found {verified.hex()}"
        )

    print(
        f"{path}: PI DOM1 pulse width "
        f"0x{old_pulse:02x} -> 0x{pulse:02x}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--pulse",
        required=True,
        type=parse_byte,
        help="PI DOM1 pulse-width byte (for example 0x40)",
    )
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    set_pi_pulse(args.rom, args.pulse)


if __name__ == "__main__":
    main()
