OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(setup_start)

SECTIONS
{
    . = 0;
    .setup : {
        *(.setup)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    ASSERT(SIZEOF(.setup) == 4608,
        "Linux boot protocol setup image must be exactly 4608 bytes")
}
