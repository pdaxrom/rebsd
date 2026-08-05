/* QEMU Malta64 kernel layout matching the N64 8 MiB RDRAM profile. */

OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
  kernel (rwx) : ORIGIN = 0x80001000, LENGTH = 960K
  uarea  (rw!x): ORIGIN = 0x800f2000, LENGTH = 8K
  romdisk (rwx) : ORIGIN = 0x80800000, LENGTH = 32768K
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

  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.got) *(.got.plt) }
}
