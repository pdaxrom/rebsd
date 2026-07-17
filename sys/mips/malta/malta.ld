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
  uarea  (rw!x): ORIGIN = 0x802fe000, LENGTH = 8K
  romdisk (rwx) : ORIGIN = 0x80800000, LENGTH = 34816K
}

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
    __malta_romdisk_start = .;
    KEEP(*(.romdisk))
    KEEP(*(.romdisk.*))
    . = ALIGN(16);
    __malta_romdisk_end = .;
  } > romdisk

  .cartflash_bss (NOLOAD) : {
    __malta_cartflash_start = .;
    KEEP(*(.cartflash_bss))
    KEEP(*(.cartflash_bss.*))
    . = ALIGN(16);
    __malta_cartflash_end = .;
  } > romdisk
  ASSERT((__malta_cartflash_end - __malta_cartflash_start) <= 0x200000,
         "malta cartflash reserve overflow")

  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.got) *(.got.plt) }
}
