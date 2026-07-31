#include "interrupt.h"
#include "io.h"

#include <machine/machparam.h>

#define PIC_MASTER_COMMAND 0x0020u
#define PIC_MASTER_DATA    0x0021u
#define PIC_SLAVE_COMMAND  0x00a0u
#define PIC_SLAVE_DATA     0x00a1u

#define PIC_ICW1_INIT      0x10u
#define PIC_ICW1_ICW4      0x01u
#define PIC_ICW4_8086      0x01u
#define PIC_EOI            0x20u
#define PIC_OCW3_READ_ISR  0x0bu
#define PIC_OCW3_READ_IRR  0x0au
#define PIC_CASCADE_IRQ    2u
#define PIC_ELCR_MASTER    0x04d0u
#define PIC_ELCR_SLAVE     0x04d1u
#define PIC_EDGE_ONLY_MASK ((1u << 0) | (1u << 1) | (1u << 2) | \
                            (1u << 8) | (1u << 13))

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
i386_pic_set_level(unsigned irq)
{
    i386_u16 port;
    i386_u8 value;
    unsigned bit;
    int state;

    if (irq >= I386_IRQ_COUNT || (PIC_EDGE_ONLY_MASK & (1u << irq)) != 0)
        return 0;
    if (irq < 8u) {
        port = PIC_ELCR_MASTER;
        bit = irq;
    } else {
        port = PIC_ELCR_SLAVE;
        bit = irq - 8u;
    }

    state = i386_intr_disable();
    value = i386_inb(port);
    i386_outb(port, value | (i386_u8)(1u << bit));
    value = i386_inb(port);
    i386_intr_restore(state);
    return (value & (1u << bit)) != 0;
}

int
i386_pic_irq_pending(unsigned irq)
{
    i386_u16 port;
    unsigned bit;

    if (irq >= I386_IRQ_COUNT)
        return 0;
    if (irq < 8u) {
        port = PIC_MASTER_COMMAND;
        bit = irq;
    } else {
        port = PIC_SLAVE_COMMAND;
        bit = irq - 8u;
    }
    i386_outb(port, PIC_OCW3_READ_IRR);
    return (i386_inb(port) & (1u << bit)) != 0;
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
