/*
 * VR4300/N64 low-level register helpers.
 */
#ifndef _N64_IO_H_
#define _N64_IO_H_

#define C0_BADVADDR     8
#define C0_COUNT        9
#define C0_COMPARE      11
#define C0_STATUS       12
#define C0_CAUSE        13
#define C0_EPC          14
#define C0_CONFIG       16
#define C0_WATCHLO      18
#define C0_WATCHHI      19
#define C0_XCONTEXT     20
#define C0_TAGLO        28
#define C0_TAGHI        29
#define C0_ERROREPC     30

#define ST_IE           0x00000001u
#define ST_EXL          0x00000002u
#define ST_ERL          0x00000004u
#define ST_KSU          0x00000018u
#define ST_KSU_USER     0x00000010u
#define ST_IM0          0x00000100u
#define ST_IM1          0x00000200u
#define ST_IM2          0x00000400u
#define ST_IM3          0x00000800u
#define ST_IM4          0x00001000u
#define ST_IM5          0x00002000u
#define ST_IM6          0x00004000u
#define ST_IM7          0x00008000u
#define ST_RP           0x08000000u
#define ST_CU0          0x10000000u
#define ST_CU1          0x20000000u

#define CA_BD           0x80000000u
#define CA_CE           0x30000000u
#define CA_IP           0x0000ff00u
#define CA_EXC_CODE     0x0000007cu

#define CA_Int          0x00000000u
#define CA_Mod          0x00000004u
#define CA_TLBL         0x00000008u
#define CA_TLBS         0x0000000cu
#define CA_AdEL         0x00000010u
#define CA_AdES         0x00000014u
#define CA_IBE          0x00000018u
#define CA_DBE          0x0000001cu
#define CA_Sys          0x00000020u
#define CA_Bp           0x00000024u
#define CA_RI           0x00000028u
#define CA_CPU          0x0000002cu
#define CA_Ov           0x00000030u
#define CA_Tr           0x00000034u
#define CA_FPE          0x0000003cu

#define FRAME_R1        0
#define FRAME_R2        1
#define FRAME_R3        2
#define FRAME_R4        3
#define FRAME_R5        4
#define FRAME_R6        5
#define FRAME_R7        6
#define FRAME_R8        7
#define FRAME_R9        8
#define FRAME_R10       9
#define FRAME_R11       10
#define FRAME_R12       11
#define FRAME_R13       12
#define FRAME_R14       13
#define FRAME_R15       14
#define FRAME_R16       15
#define FRAME_R17       16
#define FRAME_R18       17
#define FRAME_R19       18
#define FRAME_R20       19
#define FRAME_R21       20
#define FRAME_R22       21
#define FRAME_R23       22
#define FRAME_R24       23
#define FRAME_R25       24
#define FRAME_GP        25
#define FRAME_SP        26
#define FRAME_FP        27
#define FRAME_RA        28
#define FRAME_LO        29
#define FRAME_HI        30
#define FRAME_STATUS    31
#define FRAME_PC        32
#define FRAME_WORDS     33

#ifndef __ASSEMBLER__

static inline void
mips_set_stack_pointer(void *x)
{
    asm volatile ("move $sp, %0" : : "r" (x) : "sp");
}

static inline void *
mips_get_stack_pointer(void)
{
    void *x;

    asm volatile ("move %0, $sp" : "=r" (x));
    return x;
}

#define mips_read_c0_register(reg, sel) \
    ({  unsigned __value; \
        (void)(sel); \
        asm volatile ("mfc0 %0, $%1" : "=r" (__value) : "K" (reg)); \
        __value; \
    })

#define mips_write_c0_register(reg, sel, value) \
    do { \
        (void)(sel); \
        asm volatile ( \
        "mtc0 %0, $%1\n" \
        "nop\n" \
        "nop\n" \
        "nop" \
        : : "r" ((unsigned)(value)), "K" (reg)); \
    } while (0)

static inline int
mips_intr_disable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

    mips_write_c0_register(C0_STATUS, 0, status & ~ST_IE);
    return status;
}

static inline int
mips_intr_enable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

    mips_write_c0_register(C0_STATUS, 0, status | ST_IE);
    return status;
}

static inline void
mips_intr_restore(int x)
{
    mips_write_c0_register(C0_STATUS, 0, x);
}

static inline void
mips_ehb(void)
{
    asm volatile ("nop; nop; nop" ::: "memory");
}

static inline int
mips_clz(unsigned x)
{
    int n = 0;

    if (x == 0)
        return 32;
    while ((x & 0x80000000u) == 0) {
        n++;
        x <<= 1;
    }
    return n;
}

static inline unsigned
mips_bswap(unsigned x)
{
    return ((x & 0x000000ffu) << 24) |
           ((x & 0x0000ff00u) << 8) |
           ((x & 0x00ff0000u) >> 8) |
           ((x & 0xff000000u) >> 24);
}

static inline void
mips_fpu_enable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

    mips_write_c0_register(C0_STATUS, 0, status | ST_CU1);
}

static inline void
mips_fpu_disable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

    mips_write_c0_register(C0_STATUS, 0, status & ~ST_CU1);
}

#endif /* __ASSEMBLER__ */
#endif /* _N64_IO_H_ */
