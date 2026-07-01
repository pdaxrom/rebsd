# ReBSD Origin

ReBSD is a fork of RetroBSD.

RetroBSD provided the 2.11BSD-inspired base system, the historical PIC32
target, and much of the userland and toolchain foundation used in this tree.
ReBSD keeps that origin explicit while moving the fork toward MMU-enabled MIPS
systems, including Nintendo 64 and QEMU Malta.

Project identity:

- Name: ReBSD
- Description: ReBSD is a fork of RetroBSD with MMU support, ported to
  Nintendo 64 and vintage MMU-enabled MIPS hardware.

Compatibility policy:

- Copyright and historical attribution for imported RetroBSD code must remain
  intact.
- Existing RetroBSD command-line names, preprocessor macros, and target aliases
  may remain as compatibility interfaces while ReBSD names are added.
- User-visible banners, current project documentation, and new target names
  should use ReBSD.
