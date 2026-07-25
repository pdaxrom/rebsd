#include "boot.h"
#include "interrupt.h"
#include "tss.h"

#define I386_TSS_ACCESS_AVAILABLE 0x89u
#define I386_TSS_GDT_INDEX        (I386_TSS_SELECTOR >> 3)
#define I386_TSS_EMERGENCY_BYTES  4096u

struct i386_gdt_pointer {
    i386_u16 limit;
    i386_u32 base;
} __attribute__((packed));

struct i386_segment_descriptor {
    i386_u16 limit_low;
    i386_u16 base_low;
    i386_u8 base_middle;
    i386_u8 access;
    i386_u8 limit_high;
    i386_u8 base_high;
} __attribute__((packed));

struct i386_tss {
    i386_u32 previous;
    i386_u32 esp0;
    i386_u32 ss0;
    i386_u32 esp1;
    i386_u32 ss1;
    i386_u32 esp2;
    i386_u32 ss2;
    i386_u32 cr3;
    i386_u32 eip;
    i386_u32 eflags;
    i386_u32 eax;
    i386_u32 ecx;
    i386_u32 edx;
    i386_u32 ebx;
    i386_u32 esp;
    i386_u32 ebp;
    i386_u32 esi;
    i386_u32 edi;
    i386_u32 es;
    i386_u32 cs;
    i386_u32 ss;
    i386_u32 ds;
    i386_u32 fs;
    i386_u32 gs;
    i386_u32 ldt;
    i386_u16 trap;
    i386_u16 iomap_base;
} __attribute__((packed));

typedef char i386_assert_tss_size[sizeof(struct i386_tss) == 104 ? 1 : -1];

static struct i386_tss i386_tss __attribute__((aligned(16)));
static unsigned char i386_tss_emergency_stack[I386_TSS_EMERGENCY_BYTES]
    __attribute__((aligned(16)));

static void
i386_tss_zero(void *pointer, unsigned size)
{
    unsigned char *bytes;

    bytes = (unsigned char *)pointer;
    while (size-- != 0)
        *bytes++ = 0;
}

void
i386_tss_set_kernel_stack(unsigned stack_pointer)
{
    i386_tss.esp0 = stack_pointer;
    i386_tss.ss0 = I386_KERNEL_DATA_SELECTOR;
}

void
i386_tss_reset_kernel_stack(void)
{
    i386_tss_set_kernel_stack((unsigned)(unsigned long)
        &i386_tss_emergency_stack[sizeof(i386_tss_emergency_stack)]);
}

unsigned
i386_tss_kernel_stack(void)
{
    return i386_tss.esp0;
}

int
i386_tss_init(void)
{
    struct i386_segment_descriptor *descriptor;
    struct i386_gdt_pointer gdtr;
    unsigned base;
    unsigned limit;
    unsigned short selector;

    __asm__ volatile ("sgdt %0" : "=m" (gdtr));
    if ((unsigned)gdtr.limit + 1u <
        (I386_TSS_GDT_INDEX + 1u) * sizeof(*descriptor))
        return -1;

    i386_tss_zero(&i386_tss, sizeof(i386_tss));
    i386_tss_reset_kernel_stack();
    i386_tss.iomap_base = sizeof(i386_tss);

    descriptor = (struct i386_segment_descriptor *)(unsigned long)
        (gdtr.base + I386_TSS_GDT_INDEX * sizeof(*descriptor));
    base = (unsigned)(unsigned long)&i386_tss;
    limit = sizeof(i386_tss) - 1u;
    descriptor->limit_low = (i386_u16)limit;
    descriptor->base_low = (i386_u16)base;
    descriptor->base_middle = (i386_u8)(base >> 16);
    descriptor->access = I386_TSS_ACCESS_AVAILABLE;
    descriptor->limit_high = (i386_u8)((limit >> 16) & 0x0fu);
    descriptor->base_high = (i386_u8)(base >> 24);

    selector = I386_TSS_SELECTOR;
    __asm__ volatile ("ltr %0" : : "rm" (selector));
    selector = 0;
    __asm__ volatile ("str %0" : "=rm" (selector));
    return selector == I386_TSS_SELECTOR ? 0 : -1;
}
