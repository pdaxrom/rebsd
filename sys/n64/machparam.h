/*
 * Machine dependent constants for Nintendo 64 / VR4300.
 */
#ifndef ENDIAN
#define LITTLE          1234
#define BIG             4321
#define PDP             3412
#define ENDIAN          BIG

#define MAXSLP          20

#ifndef HZ
#define HZ              100
#endif

#ifndef NBUF
#define NBUF            16
#endif
#ifndef MAXUSERS
#define MAXUSERS        1
#endif
#ifndef NPROC
#define NPROC           25
#endif
#ifndef NINODE
#define NINODE          32
#endif
#ifndef NFILE
#define NFILE           32
#endif
#define NNAMECACHE      (NINODE * 11/10)
#define NCALL           (16 + 2 * MAXUSERS)
#define NCLIST          32
#ifndef SMAPSIZ
#define SMAPSIZ         NPROC
#endif

#define DEV_BSIZE       1024
#define DEV_BSHIFT      10
#define DEV_BMASK       (DEV_BSIZE-1)
#define btod(x)         (((x) + DEV_BSIZE-1) >> DEV_BSHIFT)

#define N64_RDRAM_SIZE          (4*1024*1024)
#define N64_KERNEL_RESERVED     (1024*1024)
#define N64_BASE_SWAP_RESERVED  (512*1024)

#define KERNEL_DATA_START       0x80000000
#define KERNEL_DATA_END         (KERNEL_DATA_START + N64_KERNEL_RESERVED)
#define USER_DATA_START         KERNEL_DATA_END
#define USER_DATA_END           (0x80000000 + N64_RDRAM_SIZE - N64_BASE_SWAP_RESERVED)

#define stacktop(siz)           (USER_DATA_END)
#define stackbas(siz)           (USER_DATA_END-(siz))

#define USIZE           4096
#define SSIZE           2048

#if !defined(UCB_METER) && !defined(NO_UCB_METER)
#define UCB_METER
#endif

#ifdef KERNEL
#include "machine/io.h"

#define USERMODE(ps)    (((ps) & ST_KSU) == ST_KSU_USER)
#define BASEPRI(ps)     (((ps) & ST_IM7) == 0)

#define splbio()        mips_intr_disable()
#define spltty()        mips_intr_disable()
#define splclock()      mips_intr_disable()
#define splhigh()       mips_intr_disable()
#define splnet()        mips_intr_disable()
#define splsoftclock()  mips_intr_enable()
#define spl0()          mips_intr_enable()
#define splx(s)         mips_intr_restore(s)

#define noop()          asm volatile("nop")

void idle(void);
void udelay(unsigned usec);
void clkstart(void);
void led_control(int mask, int on);

#define LED_MISC4       0x80
#define LED_MISC3       0x40
#define LED_MISC2       0x20
#define LED_MISC1       0x10
#define LED_TTY         0x08
#define LED_SWAP        0x04
#define LED_DISK        0x02
#define LED_KERNEL      0x01

#endif /* KERNEL */
#endif /* ENDIAN */
