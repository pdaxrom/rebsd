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
#define NINODE          64
#endif
#ifndef NFILE
#define NFILE           32
#endif
#ifndef NMOUNT
#define NMOUNT          8
#endif
#define NNAMECACHE      (NINODE * 11/10)
#define NCALL           (16 + 2 * MAXUSERS)
#define NCLIST          32
#define DEV_BSIZE       1024
#define DEV_BSHIFT      10
#define DEV_BMASK       (DEV_BSIZE-1)
#define btod(x)         (((x) + DEV_BSIZE-1) >> DEV_BSHIFT)

#define NBPG            1024
#define PGOFSET         (NBPG - 1)
#define CLSIZE          1
#define CLSHIFT         10
#define CLBYTES         (CLSIZE * NBPG)
#define CLOFSET         (CLBYTES - 1)
#define btoc(x)         (((x) + NBPG - 1) / NBPG)
#define ctob(x)         ((x) * NBPG)

#include <machine/layout.h>

#define N64_RDRAM_SIZE          N64_BASE_RDRAM_SIZE

#define KERNEL_DATA_START       N64_KERNEL_DATA_START
#define KERNEL_DATA_END         N64_KERNEL_DATA_END
#define USER_DATA_START         N64_USER_VADDR_START

#if defined(KERNEL) && !defined(__ASSEMBLER__)
unsigned n64_user_maxmem(void);
unsigned n64_user_data_end(void);

#define MAXMEM                  n64_user_maxmem()
#define USER_DATA_END           n64_user_data_end()
#else
#define MAXMEM                  N64_USER_MAXMEM
#define USER_DATA_END           N64_USER_VADDR_END
#endif

#define stacktop(siz)           (USER_DATA_END)
#define stackbas(siz)           (USER_DATA_END-(siz))

#define USIZE           N64_UAREA_SIZE
#define SSIZE           2048

#if !defined(UCB_METER) && !defined(NO_UCB_METER)
#define UCB_METER
#endif

#ifdef KERNEL
#include "machine/io.h"

#define USERMODE(ps)    (((ps) & ST_KSU) == ST_KSU_USER)
/*
 * The VR4300 CP0 status register has interrupt mask bits, but no PIC32-style
 * IPL field.  The N64 spl* implementation raises priority by clearing ST_IE,
 * so a saved status with ST_IE set is the closest equivalent of base priority.
 */
#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
#if defined(N64_USB_GDB) || defined(USBNET_ENABLED)
#define N64_BASEPRI_MASK (ST_IE | ST_IM2 | ST_IM3 | ST_IM7)
#else
#define N64_BASEPRI_MASK (ST_IE | ST_IM2 | ST_IM7)
#endif
#define BASEPRI(ps)     (((ps) & N64_BASEPRI_MASK) == N64_BASEPRI_MASK)
#else
#define BASEPRI(ps)     (((ps) & ST_IE) != 0)
#endif

#define splbio()        mips_intr_disable()
#define spltty()        mips_intr_disable()
#define splclock()      mips_intr_disable()
#define splhigh()       mips_intr_disable()
#define splnet()        mips_intr_disable()
#define splimp()        mips_intr_disable()
#define splsoftclock()  mips_intr_enable()
#define spl0()          mips_intr_enable()
#define splx(s)         mips_intr_restore(s)

#define noop()          asm volatile("nop")

#ifndef __ASSEMBLER__
void idle(void);
void udelay(unsigned usec);
void clkstart(void);
void led_control(int mask, int on);
void mips_sync_user_icache(void);
#endif

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
