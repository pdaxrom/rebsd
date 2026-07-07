/*
 * Linker script for the first Ci20 RAM-loaded image.
 *
 * U-Boot loads the generated uImage into KSEG0 RAM.  The root filesystem is
 * linked into the same binary at 0x80800000 so the first port does not depend
 * on SD, NAND, or USB storage.
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
  kernel (rwx) : ORIGIN = 0x80100000, LENGTH = 2032K
  u0area (rw!x): ORIGIN = 0x802fc000, LENGTH = 8K
  uarea  (rw!x): ORIGIN = 0x802fe000, LENGTH = 8K
  romdisk (rwx) : ORIGIN = 0x80800000, LENGTH = 34816K
}

u0      = ORIGIN(u0area);
u       = ORIGIN(uarea);
u_end   = ORIGIN(uarea) + LENGTH(uarea);

SECTIONS
{
  .text : {
    *(.startup)
    *(.exception .exception.*)
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
    __ci20_romdisk_start = .;
    KEEP(*(.romdisk))
    KEEP(*(.romdisk.*))
    . = ALIGN(16);
    __ci20_romdisk_end = .;
  } > romdisk

  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.got) *(.got.plt) }
}
