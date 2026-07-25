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

static inline unsigned short
i386_inw(unsigned short port)
{
    unsigned short value;

    __asm__ volatile ("inw %w1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static inline unsigned int
i386_inl(unsigned short port)
{
    unsigned int value;

    __asm__ volatile ("inl %w1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static inline void
i386_outl(unsigned short port, unsigned int value)
{
    __asm__ volatile ("outl %0, %w1" : : "a" (value), "Nd" (port));
}

static inline void
i386_io_wait(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a" (0));
}

#endif
