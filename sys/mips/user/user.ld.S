#include "../layout.h"
#include "../../vm/vm_constants.h"

#ifdef mips
#undef mips
#endif
#ifdef MIPS
#undef MIPS
#endif

#ifdef TARGET_LITTLE_ENDIAN
OUTPUT_FORMAT("elf32-littlemips", "elf32-bigmips", "elf32-littlemips")
#else
OUTPUT_FORMAT("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
#endif
OUTPUT_ARCH(mips)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  PROVIDE(__executable_start = MIPS_USER_VADDR_START);
  . = MIPS_USER_VADDR_START;

  .text : {
    _ftext = .;
    *(.text .stub .text.* .gnu.linkonce.t.*)
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.eh_frame .eh_frame.*)
    . = ALIGN(16);
  } :text

  PROVIDE(__etext = .);
  PROVIDE(_etext = .);
  PROVIDE(etext = .);

  . = ALIGN(VM_PAGE_BYTES);
  .data : {
    __data_start = .;
    _fdata = .;
    *(.data .data.* .gnu.linkonce.d.*)
    . = ALIGN(16);
    _gp = . + MIPS_USER_GP_OFFSET;
    *(.sdata .sdata.* .gnu.linkonce.s.*)
    *(.lit8)
    *(.lit4)
    . = ALIGN(4);
    PROVIDE(__CTOR_LIST__ = .);
    KEEP(*(.ctors .ctors.*))
    KEEP(*(.init_array .init_array.*))
    PROVIDE(__CTOR_END__ = .);
    PROVIDE(__DTOR_LIST__ = .);
    KEEP(*(.dtors .dtors.*))
    KEEP(*(.fini_array .fini_array.*))
    PROVIDE(__DTOR_END__ = .);
    . = ALIGN(16);
  } :data

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
  } :data

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
