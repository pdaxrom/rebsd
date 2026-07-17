/*
 * Linker script for the Nintendo 64 VR4300 kernel.
 *
 * Stage0 is expected to load this ELF32 big-endian image into RDRAM and jump
 * to _start. The first version keeps all kernel segments in KSEG0.
 */
#include "layout.h"

OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
  kernel      (rwx) : ORIGIN = N64_KERNEL_LOAD_VADDR, LENGTH = N64_KERNEL_LINK_LENGTH
  uarea       (rw!x): ORIGIN = N64_UAREA_VADDR, LENGTH = N64_UAREA_SIZE
}

u       = ORIGIN(uarea);
u_end   = ORIGIN(uarea) + LENGTH(uarea);

SECTIONS
{
  .text : {
    *(.startup)
    *(.exception)
    *(.text .text.* .stub .gnu.linkonce.t.*)
    *(.gnu.warning)
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    . = ALIGN(16);
    _etext = .;
  } > kernel

  .data : {
    __data_start = .;
    _gp = ALIGN(16);
    *(.sdata .sdata.* .gnu.linkonce.s.*)
    *(.data .data.* .gnu.linkonce.d.*)
    *(.eh_frame)
    . = ALIGN(16);
    _edata = .;
  } > kernel

  .bss (NOLOAD) : {
    __bss_start = .;
    *(.dynbss)
    *(.sbss .sbss.*)
    *(.scommon)
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(16);
  } > kernel
  __bss_end = .;
  _end = .;

  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .comment       0 : { *(.comment) }
  .debug_aranges 0 : { *(.debug_aranges) }
  .debug_info    0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev  0 : { *(.debug_abbrev) }
  .debug_line    0 : { *(.debug_line) }
  .debug_frame   0 : { *(.debug_frame) }
  .debug_str     0 : { *(.debug_str) }
  .debug_loc     0 : { *(.debug_loc) }
  .debug_macinfo 0 : { *(.debug_macinfo) }

  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.got) *(.got.plt) }
}
