#ifndef _I386_IO_H_
#define _I386_IO_H_

static inline unsigned char
i386_inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile ("inb %w1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static inline void
i386_outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a" (value), "Nd" (port));
}

static inline void
i386_io_wait(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a" (0));
}

#endif
