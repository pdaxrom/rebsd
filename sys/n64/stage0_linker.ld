OUTPUT_FORMAT("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

PHDRS
{
    irq PT_LOAD AT(0x80000000);
    main PT_LOAD AT(0x80000400);
}

SECTIONS
{
    .intvectors 0x80000000 : {
        . = ALIGN(32);
        KEEP(*(.intvectors))
    } :irq

    .text 0x80000400 : {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
        . = ALIGN(16);
        *(.rodata)
        *(.rodata.*)
        *(.rodata.kernel)
        . = ALIGN(16);
    } :main

    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.got)
        *(.got.*)
        . = ALIGN(16);
    } :main

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :main

    /DISCARD/ : {
        *(.comment)
        *(.gnu.attributes)
        *(.MIPS.abiflags)
        *(.note.*)
        *(.pdr)
        *(.reginfo)
    }
}
