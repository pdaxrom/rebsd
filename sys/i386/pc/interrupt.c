#include "boot.h"
#include "interrupt.h"
#include "process.h"
#include "syscall.h"
#include "trap.h"
#include "user_return.h"
#include "vmspace_bootstrap.h"

#include <machine/machparam.h>

#define I386_IDT_INTERRUPT_GATE   0x8eu
#define I386_IDT_USER_TRAP_GATE   0xefu
#define I386_IDT_USER_INTERRUPT_GATE 0xeeu
#define I386_EXCEPTION_BREAKPOINT 3u
#define I386_EXCEPTION_PAGE_FAULT 14u
#define I386_PAGE_FAULT_WRITE     0x02u
#define I386_PAGE_FAULT_USER      0x04u

struct i386_idt_gate {
    i386_u16 offset_low;
    i386_u16 selector;
    i386_u8 zero;
    i386_u8 attributes;
    i386_u16 offset_high;
} __attribute__((packed));

struct i386_idt_descriptor {
    i386_u16 limit;
    i386_u32 base;
} __attribute__((packed));

typedef void (*i386_vector_handler)(void);

extern i386_vector_handler const i386_vector_table[I386_VECTOR_TABLE_COUNT];
extern i386_vector_handler const i386_unhandled_vector_entry;
extern void i386_vector_128(void);

static struct i386_idt_gate i386_idt[I386_IDT_ENTRIES]
    __attribute__((aligned(16)));
static volatile i386_u32 i386_breakpoints;

struct i386_irq_registration {
    i386_irq_handler_t ir_handler;
    void *ir_arg;
};

static struct i386_irq_registration
    i386_irq_handlers[I386_IRQ_COUNT][I386_IRQ_MAX_HANDLERS];

int
i386_irq_establish(unsigned irq, i386_irq_handler_t handler, void *arg)
{
    unsigned slot;
    int state;

    if (irq >= I386_IRQ_COUNT || handler == (i386_irq_handler_t)0)
        return 0;
    state = i386_intr_disable();
    for (slot = 0; slot < I386_IRQ_MAX_HANDLERS; ++slot) {
        if (i386_irq_handlers[irq][slot].ir_handler ==
            (i386_irq_handler_t)0) {
            i386_irq_handlers[irq][slot].ir_arg = arg;
            i386_irq_handlers[irq][slot].ir_handler = handler;
            i386_intr_restore(state);
            return 1;
        }
    }
    i386_intr_restore(state);
    return 0;
}

static void
i386_idt_set_gate(unsigned vector, i386_vector_handler handler,
    i386_u8 attributes)
{
    i386_u32 offset;

    offset = (i386_u32)(unsigned long)handler;
    i386_idt[vector].offset_low = (i386_u16)(offset & 0xffffu);
    i386_idt[vector].selector = I386_KERNEL_CODE_SELECTOR;
    i386_idt[vector].zero = 0;
    i386_idt[vector].attributes = attributes;
    i386_idt[vector].offset_high = (i386_u16)(offset >> 16);
}

void
i386_idt_init(void)
{
    struct i386_idt_descriptor descriptor;
    unsigned vector;

    for (vector = 0; vector < I386_IDT_ENTRIES; ++vector)
        i386_idt_set_gate(vector, i386_unhandled_vector_entry,
            I386_IDT_INTERRUPT_GATE);
    for (vector = 0; vector < I386_IRQ_BASE + I386_IRQ_COUNT; ++vector)
        i386_idt_set_gate(vector, i386_vector_table[vector],
            I386_IDT_INTERRUPT_GATE);
    i386_idt_set_gate(I386_EXCEPTION_BREAKPOINT,
        i386_vector_table[I386_EXCEPTION_BREAKPOINT],
        I386_IDT_USER_TRAP_GATE);
    i386_idt_set_gate(I386_USER_RETURN_VECTOR,
        i386_vector_table[I386_USER_RETURN_VECTOR],
        I386_IDT_USER_TRAP_GATE);
    i386_idt_set_gate(I386_SYSCALL_VECTOR, i386_vector_128,
        I386_IDT_USER_INTERRUPT_GATE);

    descriptor.limit = (i386_u16)(sizeof(i386_idt) - 1u);
    descriptor.base = (i386_u32)(unsigned long)i386_idt;
    __asm__ volatile ("lidt %0" : : "m" (descriptor));
}

static void
i386_exception_halt(const struct i386_trapframe *frame)
{
    i386_u32 cr2;

    i386_early_puts("exception: vector=");
    i386_early_put_hex32(frame->tf_vector);
    i386_early_puts(" error=");
    i386_early_put_hex32(frame->tf_error);
    i386_early_puts(" eip=");
    i386_early_put_hex32(frame->tf_eip);
    if (frame->tf_vector == I386_EXCEPTION_PAGE_FAULT) {
        __asm__ volatile ("movl %%cr2, %0" : "=r" (cr2));
        i386_early_puts(" cr2=");
        i386_early_put_hex32(cr2);
    }
    i386_early_putc('\n');
    i386_early_puts("PANIC: cpu exception\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void
i386_interrupt_dispatch(struct i386_trapframe *frame)
{
    i386_u32 cr2;
    unsigned access;
    i386_u32 clock_ps;
    unsigned irq;
    unsigned slot;

    if (frame->tf_vector == I386_EXCEPTION_BREAKPOINT) {
        ++i386_breakpoints;
        (void)i386_user_trap(frame, frame->tf_eip);
        return;
    }

    if (frame->tf_vector == I386_USER_RETURN_VECTOR) {
        if (i386_privilege_handle_return(frame))
            return;
        if (i386_syscall_handle_return(frame))
            return;
        if (i386_user_return_handle_test(frame))
            return;
        if (i386_trap_handle_test(frame))
            return;
        if (i386_process_handle_return(frame))
            return;
        i386_exception_halt(frame);
    }

    if (frame->tf_vector == I386_SYSCALL_VECTOR) {
        i386_syscall_dispatch(frame);
        return;
    }

    if (frame->tf_vector == I386_EXCEPTION_PAGE_FAULT) {
        __asm__ volatile ("movl %%cr2, %0" : "=r" (cr2));
        access = (frame->tf_error & I386_PAGE_FAULT_WRITE) != 0 ?
            0x02u : 0x01u;
        if (i386_vmspace_fault_active(cr2, access,
            (frame->tf_error & I386_PAGE_FAULT_USER) != 0 ||
            (frame->tf_cs & 3u) == 3u) == 0)
            return;
        if (i386_user_trap(frame, cr2))
            return;
    }

    if (frame->tf_vector < I386_IRQ_BASE) {
        if (i386_user_trap(frame, frame->tf_eip))
            return;
        i386_exception_halt(frame);
    }

    if (frame->tf_vector >= I386_IRQ_BASE + I386_IRQ_COUNT)
        i386_exception_halt(frame);

    irq = frame->tf_vector - I386_IRQ_BASE;
    if (!i386_pic_accept_irq(irq))
        return;

    if (irq == I386_IRQ_TIMER) {
        /*
         * hardclock's MD ps contract is expressed through USERMODE and
         * BASEPRI.  Preserve the interrupted CPL from CS and IF from the
         * saved EFLAGS in the single value consumed by those macros.
         */
        clock_ps = (frame->tf_cs & 3u) |
            (frame->tf_eflags & I386_EFLAGS_INTERRUPT);
        i386_pit_interrupt(frame->tf_eip, clock_ps);
    }
    for (slot = 0; slot < I386_IRQ_MAX_HANDLERS; ++slot)
        if (i386_irq_handlers[irq][slot].ir_handler !=
            (i386_irq_handler_t)0)
            (void)i386_irq_handlers[irq][slot].ir_handler(
                i386_irq_handlers[irq][slot].ir_arg);

    i386_pic_eoi(irq);
}

void
i386_breakpoint_selftest(void)
{
    __asm__ volatile ("int3");
}

i386_u32
i386_breakpoint_count(void)
{
    return i386_breakpoints;
}
