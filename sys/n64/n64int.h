#ifndef _N64_N64INT_H_
#define _N64_N64INT_H_

#define N64_MI_INTERRUPT_SP     0x00000001u
#define N64_MI_INTERRUPT_SI     0x00000002u
#define N64_MI_INTERRUPT_AI     0x00000004u
#define N64_MI_INTERRUPT_VI     0x00000008u
#define N64_MI_INTERRUPT_PI     0x00000010u
#define N64_MI_INTERRUPT_DP     0x00000020u
#define N64_MI_INTERRUPT_ALL    (N64_MI_INTERRUPT_SP | \
                                 N64_MI_INTERRUPT_SI | \
                                 N64_MI_INTERRUPT_AI | \
                                 N64_MI_INTERRUPT_VI | \
                                 N64_MI_INTERRUPT_PI | \
                                 N64_MI_INTERRUPT_DP)

void n64_interrupt_init(void);
void n64_interrupt_handle_mi(void);
unsigned n64_mi_pending(void);
void n64_mi_enable(unsigned mask);
void n64_mi_disable(unsigned mask);
void n64_mi_ack(unsigned mask);

#endif
