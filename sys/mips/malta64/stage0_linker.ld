OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
  stage0 (rwx) : ORIGIN = 0xffffffff80010000, LENGTH = 30M
}

SECTIONS
{
  .text : {
    *(.startup)
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(16);
  } > stage0

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
    . = ALIGN(16);
  } > stage0

  .kernel_blob : {
    . = ALIGN(16);
    KEEP(*(.kernel_blob))
    . = ALIGN(16);
  } > stage0

  .bss (NOLOAD) : {
    *(.sbss .sbss.*)
    *(.scommon)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
  } > stage0

  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.got) *(.got.plt) }
}
