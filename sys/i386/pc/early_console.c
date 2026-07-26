#include "boot.h"
#include "io.h"

#include <sys/reboot.h>
#include <sys/types.h>

#define COM1_BASE       0x03f8u
#define COM_DATA        0u
#define COM_IER         1u
#define COM_FIFO        2u
#define COM_LINE_CTRL   3u
#define COM_MODEM_CTRL  4u
#define COM_LINE_STATUS 5u
#define COM_LSR_TX_IDLE 0x20u

#define VGA_TEXT_BASE   0x000b8000u
#define VGA_COLUMNS     80u
#define VGA_ROWS        25u
#define VGA_ATTRIBUTE   0x07u

static unsigned vga_row;
static unsigned vga_column;

static void
i386_serial_init(void)
{
    i386_outb(COM1_BASE + COM_IER, 0x00);
    i386_outb(COM1_BASE + COM_LINE_CTRL, 0x80);
    i386_outb(COM1_BASE + COM_DATA, 0x01);
    i386_outb(COM1_BASE + COM_IER, 0x00);
    i386_outb(COM1_BASE + COM_LINE_CTRL, 0x03);
    i386_outb(COM1_BASE + COM_FIFO, 0xc7);
    i386_outb(COM1_BASE + COM_MODEM_CTRL, 0x0b);
}

static void
i386_serial_putc(char ch)
{
    unsigned spins;

    for (spins = 0; spins < 100000u; ++spins) {
        if ((i386_inb(COM1_BASE + COM_LINE_STATUS) &
            COM_LSR_TX_IDLE) != 0)
            break;
    }
    i386_outb(COM1_BASE + COM_DATA, (i386_u8)ch);
}

static void
i386_vga_clear(void)
{
    volatile i386_u16 *vga;
    unsigned index;

    vga = (volatile i386_u16 *)VGA_TEXT_BASE;
    for (index = 0; index < VGA_COLUMNS * VGA_ROWS; ++index)
        vga[index] = (i386_u16)((VGA_ATTRIBUTE << 8) | ' ');
    vga_row = 0;
    vga_column = 0;
}

static void
i386_vga_scroll(void)
{
    volatile i386_u16 *vga;
    unsigned row;
    unsigned column;

    vga = (volatile i386_u16 *)VGA_TEXT_BASE;
    for (row = 1; row < VGA_ROWS; ++row) {
        for (column = 0; column < VGA_COLUMNS; ++column) {
            vga[(row - 1) * VGA_COLUMNS + column] =
                vga[row * VGA_COLUMNS + column];
        }
    }
    for (column = 0; column < VGA_COLUMNS; ++column) {
        vga[(VGA_ROWS - 1) * VGA_COLUMNS + column] =
            (i386_u16)((VGA_ATTRIBUTE << 8) | ' ');
    }
    vga_row = VGA_ROWS - 1;
}

static void
i386_vga_putc(char ch)
{
    volatile i386_u16 *vga;

    vga = (volatile i386_u16 *)VGA_TEXT_BASE;
    if (ch == '\r') {
        vga_column = 0;
        return;
    }
    if (ch == '\n') {
        vga_column = 0;
        ++vga_row;
    } else {
        vga[vga_row * VGA_COLUMNS + vga_column] =
            (i386_u16)((VGA_ATTRIBUTE << 8) | (i386_u8)ch);
        ++vga_column;
        if (vga_column == VGA_COLUMNS) {
            vga_column = 0;
            ++vga_row;
        }
    }
    if (vga_row == VGA_ROWS)
        i386_vga_scroll();
}

void
i386_early_console_init(void)
{
    i386_serial_init();
    i386_vga_clear();
}

void
i386_early_putc(char ch)
{
    if (ch == '\n')
        i386_serial_putc('\r');
    i386_serial_putc(ch);
    i386_vga_putc(ch);
}

void
cnputc(char ch)
{
    i386_early_putc(ch);
}

void
i386_early_puts(const char *text)
{
    while (*text != '\0')
        i386_early_putc(*text++);
}

void
boot(dev_t dev, int howto)
{
    (void)dev;
    __asm__ volatile ("cli");
    if ((howto & RB_HALT) == 0)
        i386_early_puts("reboot: unsupported\n");
    i386_early_puts("halted\n");
    for (;;)
        __asm__ volatile ("hlt");
}

static void
i386_early_put_hex_digits(i386_u32 value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    for (shift = 28; shift >= 0; shift -= 4)
        i386_early_putc(digits[(value >> (unsigned)shift) & 0x0f]);
}

void
i386_early_put_hex32(i386_u32 value)
{
    i386_early_puts("0x");
    i386_early_put_hex_digits(value);
}

void
i386_early_put_hex64(i386_u32 high, i386_u32 low)
{
    i386_early_puts("0x");
    i386_early_put_hex_digits(high);
    i386_early_put_hex_digits(low);
}
