#include <sys/param.h>
#include <machine/console.h>
#include <machine/n64.h>
#include <machine/rompak.h>

static void
puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            n64_console_putc('\r');
        n64_console_putc(*s++);
    }
}

static void
put_hex32(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        n64_console_putc(digits[(value >> (unsigned)shift) & 0x0f]);
}

int
main(void)
{
    struct n64_rompak_entry rootfs;

    puts("\nReBSD N64 preflight\n");
    puts("rdram size=");
    put_hex32(n64_rdram_size());
    puts("\n");

    if (n64_rompak_find("rootfs.img", &rootfs) == 0) {
        puts("rootfs.img offset=");
        put_hex32(rootfs.offset);
        puts(" size=");
        put_hex32(rootfs.size);
        puts(" magic=");
        put_hex32(n64_rompak_read32(rootfs.offset));
        puts("\n");
    } else {
        puts("rootfs.img not found in ROM TOC\n");
    }

    for (;;)
        ;
}
