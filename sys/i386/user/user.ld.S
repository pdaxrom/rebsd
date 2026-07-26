#include "../include/machine/layout.h"
#include "../../vm/vm_constants.h"

#ifdef i386
#undef i386
#endif

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    PROVIDE(__executable_start = I386_USER_VADDR_START);
    . = I386_USER_VADDR_START;

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(16);
    } :text

    PROVIDE(__etext = .);
    PROVIDE(_etext = .);
    PROVIDE(etext = .);

    . = ALIGN(VM_PAGE_BYTES);
    .data : {
        __data_start = .;
        *(.data .data.*)
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

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
    } :data

    _end = .;
    PROVIDE(end = .);

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
        *(.comment)
        *(.eh_frame .eh_frame.*)
    }

    ASSERT(_end < I386_USER_VADDR_END,
        "i386 user executable exceeds the user address range")
}
