#ifndef _I386_INTERRUPT_H_
#define _I386_INTERRUPT_H_

#include "boot.h"

#define I386_IDT_ENTRIES       256u
#define I386_IRQ_BASE          32u
#define I386_IRQ_COUNT         16u
#define I386_IRQ_TIMER         0u
#define I386_PIT_HZ            100u
#define I386_KERNEL_CODE_SELECTOR 0x0008u
#define I386_KERNEL_DATA_SELECTOR 0x0010u

/*
 * Stack layout built by interrupt_entry.S.  The processor does not push
 * user ESP/SS yet because the early kernel runs entirely at privilege 0.
 */
struct i386_trapframe {
    i386_u32 tf_gs;
    i386_u32 tf_fs;
    i386_u32 tf_es;
    i386_u32 tf_ds;
    i386_u32 tf_edi;
    i386_u32 tf_esi;
    i386_u32 tf_ebp;
    i386_u32 tf_esp;
    i386_u32 tf_ebx;
    i386_u32 tf_edx;
    i386_u32 tf_ecx;
    i386_u32 tf_eax;
    i386_u32 tf_vector;
    i386_u32 tf_error;
    i386_u32 tf_eip;
    i386_u32 tf_cs;
    i386_u32 tf_eflags;
    /* Present only when a later user-mode trap changes privilege level. */
    i386_u32 tf_useresp;
    i386_u32 tf_ss;
};

void i386_idt_init(void);
void i386_interrupt_dispatch(struct i386_trapframe *frame);
void i386_breakpoint_selftest(void);
i386_u32 i386_breakpoint_count(void);

void i386_pic_init(void);
void i386_pic_unmask(unsigned irq);
int i386_pic_accept_irq(unsigned irq);
void i386_pic_eoi(unsigned irq);

void i386_pit_init(void);
void i386_pit_interrupt(void);
i386_u32 i386_pit_ticks(void);
void i386_pit_wait(i386_u32 ticks);

#endif
