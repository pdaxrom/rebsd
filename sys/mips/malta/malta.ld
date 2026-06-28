/*
 * Linker script for the initial QEMU Malta bring-up image.
 *
 * KSEG0 0x80100000 maps to physical 0x00100000.  QEMU's MIPS loader
 * normalizes KSEG0 addresses before checking overlap with the Malta firmware
 * area, so this avoids the prom area around physical 0x00002000.
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
  kernel (rwx) : ORIGIN = 0x80100000, LENGTH = 2032K
  u0area (rw!x): ORIGIN = 0x802fc000, LENGTH = 8K
  uarea  (rw!x): ORIGIN = 0x802fe000, LENGTH = 8K
  romdisk (rx) : ORIGIN = 0x80600000, LENGTH = 16384K
}

u0      = ORIGIN(u0area);
u       = ORIGIN(uarea);
u_end   = ORIGIN(uarea) + LENGTH(uarea);

SECTIONS
{
  .text : {
    *(.startup)
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(16);
  } > kernel

  .data : {
    __data_start = .;
    _gp = ALIGN(16);
    *(.sdata .sdata.*)
    *(.data .data.*)
    *(.eh_frame)
    . = ALIGN(16);
    _edata = .;
  } > kernel

  .bss (NOLOAD) : {
    *(.sbss .sbss.*)
    *(.scommon)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
  } > kernel
  __bss_end = .;
  _end = .;

  .romdisk : {
    __malta_romdisk_start = .;
    KEEP(*(.romdisk))
    KEEP(*(.romdisk.*))
    . = ALIGN(16);
    __malta_romdisk_end = .;
  } > romdisk

  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.got) *(.got.plt) }
}
