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
    . = 0x00100000;
    __kernel_start = .;

    .text ALIGN(16) : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    .rodata ALIGN(16) : {
        *(.rodata .rodata.*)
    } :text

    .romdisk ALIGN(16) : {
        __i386_romdisk_start = .;
        KEEP(*(.romdisk))
        KEEP(*(.romdisk.*))
        __i386_romdisk_end = .;
    } :text

    __kernel_ro_end = .;

    .data ALIGN(4096) : {
        __kernel_rw_start = .;
        *(.data .data.*)
    } :data

    __bss_start = ALIGN(16);
    .bss __bss_start (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    __bss_end = ALIGN(16);

    __bootstrap_uarea_start = __bss_end;
    .bootstrap_uarea __bootstrap_uarea_start (NOLOAD) : {
        . = . + 16384;
    } :data
    __bootstrap_uarea_end = .;

    __kernel_end = ALIGN(4096);

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    ASSERT(_start == 0x00100000,
        "i686 protected-mode entry must be linked at 1 MiB")
}
