/*
 * MIPS low-level register helpers used by the N64 and Malta ports.
 */
#ifndef _MIPS_IO_H_
#define _MIPS_IO_H_

#define C0_INDEX        0
#define C0_ENTRYLO0     2
#define C0_ENTRYLO1     3
#define C0_PAGEMASK     5
#define C0_WIRED        6
#define C0_BADVADDR     8
#define C0_COUNT        9
#define C0_ENTRYHI      10
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
#define ST_UX           0x00000020u
#define ST_SX           0x00000040u
#define ST_KX           0x00000080u
#define ST_IM0          0x00000100u
#define ST_IM1          0x00000200u
#define ST_IM2          0x00000400u
#define ST_IM3          0x00000800u
#define ST_IM4          0x00001000u
#define ST_IM5          0x00002000u
#define ST_IM6          0x00004000u
#define ST_IM7          0x00008000u
#define ST_IM           0x0000ff00u
#define ST_BEV          0x00400000u
#define ST_FR           0x04000000u
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

/*
 * The kernel ABI is 32-bit on every current MIPS target.  VR4300 user code,
 * however, is allowed to use MIPS III 64-bit GPR instructions while running
 * under the o32 ABI.  A 32-bit exception frame would therefore corrupt a
 * live 64-bit temporary whenever a timer or TLB exception lands between an
 * ld and sd.
 *
 * On MIPS III targets each GPR/LO/HI occupies an aligned 64-bit slot.  The
 * FRAME_* index names the ABI-visible low word in that slot, so the existing
 * int * kernel interface remains 32-bit.  N64 and Malta64 are big-endian;
 * the high word precedes the low word.  MIPS32 targets retain the compact
 * historical frame.
 */
#if defined(N64) || defined(MIPS3) || defined(MALTA64)
#define MIPS_FRAME_GPR64 1
#define FRAME_R1        1
#define FRAME_R2        3
#define FRAME_R3        5
#define FRAME_R4        7
#define FRAME_R5        9
#define FRAME_R6        11
#define FRAME_R7        13
#define FRAME_R8        15
#define FRAME_R9        17
#define FRAME_R10       19
#define FRAME_R11       21
#define FRAME_R12       23
#define FRAME_R13       25
#define FRAME_R14       27
#define FRAME_R15       29
#define FRAME_R16       31
#define FRAME_R17       33
#define FRAME_R18       35
#define FRAME_R19       37
#define FRAME_R20       39
#define FRAME_R21       41
#define FRAME_R22       43
#define FRAME_R23       45
#define FRAME_R24       47
#define FRAME_R25       49
#define FRAME_GP        51
#define FRAME_SP        53
#define FRAME_FP        55
#define FRAME_RA        57
#define FRAME_LO        59
#define FRAME_HI        61
#define FRAME_STATUS    62
#define FRAME_PC        63
#define FRAME_WORDS     64
#define FRAME_GPR_STRIDE 2
#else
#define MIPS_FRAME_GPR64 0
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
#define FRAME_GPR_STRIDE 1
#endif

#define FRAME_STACK_BYTES (16 + FRAME_WORDS * 4)

#define TLB_ENTRYLO_G   0x00000001u
#define TLB_ENTRYLO_V   0x00000002u
#define TLB_ENTRYLO_D   0x00000004u
#define TLB_ENTRYLO_C_SHIFT 3
#define TLB_CACHE_UNCACHED 2u
#define TLB_CACHE_CNC   3u
#define TLB_PAGEMASK_4K 0x00000000u
#define TLB_PAGEMASK_64K 0x0001e000u
#define TLB_PAGEMASK_1M 0x001fe000u
#define TLB_PAGEMASK_256M 0x1fffe000u

#ifndef __ASSEMBLER__

/*
 * Give each invalid TLB slot a distinct VPN in unmapped KSEG0.  TLB probes
 * must never encounter these addresses during ordinary mapped user access.
 */
static inline unsigned
mips_tlb_invalid_entryhi(unsigned index)
{
    return 0x80000000u + index * 0x00002000u;
}

static inline int
mips_frame_is_gpr_word(unsigned word)
{
    return word >= FRAME_R1 && word <= FRAME_HI &&
        (word - FRAME_R1) % FRAME_GPR_STRIDE == 0;
}

static inline int
mips_frame_is_writable_word(unsigned word)
{
    return mips_frame_is_gpr_word(word) || word == FRAME_STATUS ||
        word == FRAME_PC;
}

static inline void
mips_frame_set_gpr(int *frame, unsigned word, int value)
{
    frame[word] = value;
#if MIPS_FRAME_GPR64
    frame[word - 1] = value < 0 ? -1 : 0;
#endif
}

static inline void
mips_frame_normalize_gprs(int *frame)
{
#if MIPS_FRAME_GPR64
    unsigned word;

    for (word = FRAME_R1; word <= FRAME_HI; word += FRAME_GPR_STRIDE)
        frame[word - 1] = frame[word] < 0 ? -1 : 0;
#else
    (void)frame;
#endif
}

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

static inline void
mips_tlb_write_indexed(unsigned index, unsigned pagemask, unsigned entryhi,
    unsigned entrylo0, unsigned entrylo1)
{
    asm volatile (
        "mtc0   %0, $0\n"
        "mtc0   %1, $5\n"
        "mtc0   %2, $10\n"
        "mtc0   %3, $2\n"
        "mtc0   %4, $3\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "tlbwi\n"
        "nop\n"
        "nop\n"
        "nop"
        : : "r" (index), "r" (pagemask), "r" (entryhi),
            "r" (entrylo0), "r" (entrylo1)
        : "memory");
}

static inline int
mips_intr_disable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

#if defined(N64) && (defined(N64_USB_GDB) || defined(N64_RESET_DUMP))
    /*
     * Leave the debugger IP3 and reset pre-NMI IP4 sources unmasked while
     * masking the normal MI (IP2) and timer (IP7) sources in kernel critical
     * sections.  USB networking is not an emergency path, so its IP3 must be
     * masked like the other ordinary device sources.  The GDB IP3 and reset
     * IP4 paths use private/static storage and never enter the scheduler, VM,
     * tty, or network layers.
     */
    mips_write_c0_register(C0_STATUS, 0,
        status & ~(ST_IM2 | ST_IM7
#ifndef N64_USB_GDB
        | ST_IM3
#endif
        ));
#else
    mips_write_c0_register(C0_STATUS, 0, status & ~ST_IE);
#endif
    return status;
}

static inline int
mips_intr_enable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

#if defined(N64) && (defined(N64_USB_GDB) || defined(N64_RESET_DUMP))
    mips_write_c0_register(C0_STATUS, 0,
        status | ST_IE | ST_IM2
#if defined(N64_USB_GDB) || defined(USBNET_ENABLED)
        | ST_IM3
#endif
#ifdef N64_RESET_DUMP
        | ST_IM4
#endif
        | ST_IM7);
#else
    mips_write_c0_register(C0_STATUS, 0, status | ST_IE);
#endif
    return status;
}

static inline int
mips_intr_enabled(void)
{
#if defined(N64) && (defined(N64_USB_GDB) || defined(N64_RESET_DUMP))
    unsigned status = mips_read_c0_register(C0_STATUS, 0);
    unsigned mask = ST_IE | ST_IM2 | ST_IM7;

#if defined(N64_USB_GDB) || defined(USBNET_ENABLED)
    mask |= ST_IM3;
#endif
    return (status & mask) == mask;
#else
    return (mips_read_c0_register(C0_STATUS, 0) & ST_IE) != 0;
#endif
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

    mips_write_c0_register(C0_STATUS, 0, (status | ST_CU1) & ~ST_FR);
    mips_ehb();
}

static inline void
mips_fpu_disable(void)
{
    unsigned status = mips_read_c0_register(C0_STATUS, 0);

    mips_write_c0_register(C0_STATUS, 0, status & ~(ST_CU1 | ST_FR));
    mips_ehb();
}

int mips_in_interrupt(void);

#endif /* __ASSEMBLER__ */
#endif /* _MIPS_IO_H_ */
