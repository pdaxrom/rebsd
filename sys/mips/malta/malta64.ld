/*
 * Linker script for QEMU mips64 Malta probes.
 *
 * qemu-system-mips64 enters a 64-bit kernel through a sign-extended KSEG0
 * address.  The normal 32-bit Malta ELF entry is zero-extended by the Malta
 * firmware path and faults before reaching _start on R4000/VR-family CPUs.
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
  kernel (rwx) : ORIGIN = 0xffffffff80100000, LENGTH = 2032K
  uarea  (rw!x): ORIGIN = 0xffffffff802fe000, LENGTH = 8K
  romdisk (rwx) : ORIGIN = 0xffffffff80800000, LENGTH = 32768K
}

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
