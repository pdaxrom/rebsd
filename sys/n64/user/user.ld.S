#include "../layout.h"

OUTPUT_FORMAT("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

PHDRS
{
  user PT_LOAD FLAGS(7);
}

SECTIONS
{
  PROVIDE(__executable_start = N64_USER_VADDR_START);
  . = N64_USER_VADDR_START;

  .text : {
    _ftext = .;
    *(.text .stub .text.* .gnu.linkonce.t.*)
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    . = ALIGN(16);
  } :user

  PROVIDE(__etext = .);
  PROVIDE(_etext = .);
  PROVIDE(etext = .);

  .data : {
    __data_start = .;
    _fdata = .;
    *(.data .data.* .gnu.linkonce.d.*)
    . = ALIGN(16);
    _gp = . + N64_USER_GP_OFFSET;
    *(.sdata .sdata.* .gnu.linkonce.s.*)
    *(.lit8)
    *(.lit4)
    . = ALIGN(16);
  } :user

  _edata = .;
  PROVIDE(edata = .);
  __bss_start = .;
  _fbss = .;

  .bss (NOLOAD) : {
    *(.dynsbss)
    *(.sbss .sbss.*)
    *(.scommon)
    *(.dynbss)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
  } :user

  _end = .;
  PROVIDE(end = .);

  /DISCARD/ : {
    *(.reginfo)
    *(.MIPS.abiflags)
    *(.MIPS.options)
    *(.pdr)
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.comment)
  }
}
