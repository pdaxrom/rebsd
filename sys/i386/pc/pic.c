#include "interrupt.h"
#include "io.h"

#define PIC_MASTER_COMMAND 0x0020u
#define PIC_MASTER_DATA    0x0021u
#define PIC_SLAVE_COMMAND  0x00a0u
#define PIC_SLAVE_DATA     0x00a1u

#define PIC_ICW1_INIT      0x10u
#define PIC_ICW1_ICW4      0x01u
#define PIC_ICW4_8086      0x01u
#define PIC_EOI            0x20u
#define PIC_OCW3_READ_ISR  0x0bu
#define PIC_CASCADE_IRQ    2u

static i386_u8 i386_pic_master_mask = 0xffu;
static i386_u8 i386_pic_slave_mask = 0xffu;

static i386_u8
i386_pic_read_isr(i386_u16 command_port)
{
    i386_outb(command_port, PIC_OCW3_READ_ISR);
    return i386_inb(command_port);
}

void
i386_pic_init(void)
{
    __asm__ volatile ("cli");

    i386_outb(PIC_MASTER_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    i386_io_wait();
    i386_outb(PIC_SLAVE_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    i386_io_wait();

    i386_outb(PIC_MASTER_DATA, I386_IRQ_BASE);
    i386_io_wait();
    i386_outb(PIC_SLAVE_DATA, I386_IRQ_BASE + 8u);
    i386_io_wait();

    i386_outb(PIC_MASTER_DATA, 1u << PIC_CASCADE_IRQ);
    i386_io_wait();
    i386_outb(PIC_SLAVE_DATA, PIC_CASCADE_IRQ);
    i386_io_wait();

    i386_outb(PIC_MASTER_DATA, PIC_ICW4_8086);
    i386_io_wait();
    i386_outb(PIC_SLAVE_DATA, PIC_ICW4_8086);
    i386_io_wait();

    i386_pic_master_mask = 0xffu;
    i386_pic_slave_mask = 0xffu;
    i386_outb(PIC_MASTER_DATA, i386_pic_master_mask);
    i386_outb(PIC_SLAVE_DATA, i386_pic_slave_mask);
}

void
i386_pic_unmask(unsigned irq)
{
    if (irq < 8u) {
        i386_pic_master_mask &= (i386_u8)~(1u << irq);
        i386_outb(PIC_MASTER_DATA, i386_pic_master_mask);
        return;
    }
    if (irq < I386_IRQ_COUNT) {
        i386_pic_slave_mask &= (i386_u8)~(1u << (irq - 8u));
        i386_pic_master_mask &= (i386_u8)~(1u << PIC_CASCADE_IRQ);
        i386_outb(PIC_SLAVE_DATA, i386_pic_slave_mask);
        i386_outb(PIC_MASTER_DATA, i386_pic_master_mask);
    }
}

int
i386_pic_accept_irq(unsigned irq)
{
    if (irq == 7u &&
        (i386_pic_read_isr(PIC_MASTER_COMMAND) & 0x80u) == 0)
        return 0;

    if (irq == 15u &&
        (i386_pic_read_isr(PIC_SLAVE_COMMAND) & 0x80u) == 0) {
        i386_outb(PIC_MASTER_COMMAND, PIC_EOI);
        return 0;
    }

    return 1;
}

void
i386_pic_eoi(unsigned irq)
{
    if (irq >= 8u)
        i386_outb(PIC_SLAVE_COMMAND, PIC_EOI);
    i386_outb(PIC_MASTER_COMMAND, PIC_EOI);
}
