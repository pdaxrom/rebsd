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

#define N64_SP_STATUS_ADDR      0xa4040010u
#define N64_MI_MODE_ADDR        0xa4300000u
#define N64_MI_INTERRUPT_ADDR   0xa4300008u
#define N64_MI_MASK_ADDR        0xa430000cu
#define N64_VI_CURRENT_ADDR     0xa4400010u
#define N64_AI_STATUS_ADDR      0xa450000cu
#define N64_PI_STATUS_ADDR      0xa4600010u
#define N64_SI_STATUS_ADDR      0xa4800018u

#define N64_MI_WMODE_CLR_DPINT  0x00000800u

#define N64_MI_WMASK_CLR_SP     0x00000001u
#define N64_MI_WMASK_SET_SP     0x00000002u
#define N64_MI_WMASK_CLR_SI     0x00000004u
#define N64_MI_WMASK_SET_SI     0x00000008u
#define N64_MI_WMASK_CLR_AI     0x00000010u
#define N64_MI_WMASK_SET_AI     0x00000020u
#define N64_MI_WMASK_CLR_VI     0x00000040u
#define N64_MI_WMASK_SET_VI     0x00000080u
#define N64_MI_WMASK_CLR_PI     0x00000100u
#define N64_MI_WMASK_SET_PI     0x00000200u
#define N64_MI_WMASK_CLR_DP     0x00000400u
#define N64_MI_WMASK_SET_DP     0x00000800u

#define N64_SP_CLEAR_INTERRUPT  0x00000008u
#define N64_PI_CLEAR_INTERRUPT  0x00000002u

void n64_interrupt_init(void);
void n64_interrupt_shutdown(void);
void n64_interrupt_handle_mi(void);
unsigned n64_mi_pending(void);
void n64_mi_enable(unsigned mask);
void n64_mi_disable(unsigned mask);
void n64_mi_ack(unsigned mask);

#endif
