#ifndef _MIPS_LAYOUT_H_
#define _MIPS_LAYOUT_H_

#define MIPS_SIZE_512K                 0x00080000
#define MIPS_SIZE_1M                   0x00100000
#define MIPS_SIZE_2M                   0x00200000
#define MIPS_SIZE_4M                   0x00400000
#define MIPS_SIZE_8M                   0x00800000
#define MIPS_SIZE_16M                  0x01000000
#define MIPS_SIZE_32M                  0x02000000
#define MIPS_SIZE_64M                  0x04000000

#define MIPS_KSEG0_BASE                0x80000000
#define MIPS_KSEG1_BASE                0xa0000000
#define MIPS_KSEG_PHYS_MASK            0x1fffffff
#define MIPS_PHYS_TO_KSEG0(x)          ((void *)(MIPS_KSEG0_BASE | (unsigned)(x)))
#define MIPS_PHYS_TO_KSEG1(x)          ((void *)(MIPS_KSEG1_BASE | (unsigned)(x)))
#define MIPS_KSEG_TO_PHYS(x)           ((unsigned)(x) & MIPS_KSEG_PHYS_MASK)

/*
 * Initial Malta layout for QEMU bring-up.
 *
 *   0x00000000..0x000fffff  firmware/vectors/unused low RAM
 *   0x00100000..0x002fbfff  kernel ELF
 *   0x002fc000..0x002fdfff  free after removal of historical u0
 *   0x002fe000..0x002fffff  proc0 bootstrap u area/kernel stack
 *   0x00300000..0x006fffff  legacy user-window reserve (unwired after proc1)
 *   0x00700000..0x007fffff  general-purpose RAM
 *   0x00800000..             root filesystem loaded by QEMU outside physmem
 *                             in low-memory smoke configurations
 *                             followed by Malta cartflash sparse storage
 *
 * Malta PCC smoke builds may override the RAM and root filesystem sizes from
 * the board makefile when the staged userland no longer fits in 16 MiB.
 */
#define MALTA_PHYS_RAM_BASE            0x00000000
#ifdef MALTA_RAM_SIZE_OVERRIDE
#define MALTA_RAM_SIZE                 MALTA_RAM_SIZE_OVERRIDE
#else
#define MALTA_RAM_SIZE                 MIPS_SIZE_32M
#endif
#define MALTA_CPU_KHZ                  100000u
#define MIPS_COUNT_KHZ                 50000u
#ifdef MALTA_N64_8M_PROFILE
#define MALTA_KERNEL_LOAD_VADDR        0x80001000
#define MALTA_KERNEL_LINK_LENGTH       0x000f0000
#define MALTA_KERNEL_DATA_START        MIPS_KSEG0_BASE
#define MALTA_KERNEL_DATA_END          (MIPS_KSEG0_BASE + MIPS_SIZE_1M)
#define MALTA_UAREA_SIZE               0x00002000
#define MALTA_UAREA_VADDR              0x800f2000
#else
#define MALTA_KERNEL_LOAD_VADDR        0x80100000
#define MALTA_KERNEL_LINK_LENGTH       MIPS_SIZE_2M
#define MALTA_KERNEL_DATA_START        MIPS_KSEG0_BASE
#define MALTA_KERNEL_DATA_END          (MIPS_KSEG0_BASE + 0x00300000)
#define MALTA_UAREA_SIZE               0x00002000
#define MALTA_UAREA_VADDR              0x802fe000
#endif

#define MIPS_USER_VADDR_START          0x00400000
#ifdef MALTA_N64_8M_PROFILE
#define MIPS_USER_PHYS_START           MIPS_SIZE_1M
#else
#define MIPS_USER_PHYS_START           0x00300000
#endif
#define MIPS_USER_TLB_PAGE_SIZE        MIPS_SIZE_1M
#define MIPS_USER_TLB_PAIR_SIZE        (2 * MIPS_USER_TLB_PAGE_SIZE)
#define MIPS_USER_TLB_PAIRS            2
#define MIPS_LEGACY_USER_BYTES         (MIPS_USER_TLB_PAIRS * \
                                         MIPS_USER_TLB_PAIR_SIZE)
#ifdef MALTA_N64_8M_PROFILE
#define MIPS_USER_MAXMEM               MIPS_SIZE_4M
#else
#define MIPS_USER_MAXMEM               MIPS_SIZE_64M
#endif
#define MIPS_USER_VADDR_END            (MIPS_USER_VADDR_START + MIPS_USER_MAXMEM)
#define MIPS_USER_GP_OFFSET            0x00007ff0

#ifdef MALTA_N64_8M_PROFILE
#define MALTA_STAGE0_PHYS_START        0x00300000
#define MALTA_STAGE0_BYTES             MIPS_SIZE_512K
#define MALTA_FRAMEBUFFER_PHYS_START   0x00500000
#define MALTA_FRAMEBUFFER_BYTES        0x00040000
#endif
#define MALTA_ROMDISK_PHYS_START       0x00800000
#ifdef MALTA_ROMDISK_BYTES_OVERRIDE
#define MALTA_ROMDISK_BYTES            MALTA_ROMDISK_BYTES_OVERRIDE
#else
#define MALTA_ROMDISK_BYTES            MIPS_SIZE_16M
#endif
#define MALTA_CARTFLASH_PHYS_START     (MALTA_ROMDISK_PHYS_START + \
                                         MALTA_ROMDISK_BYTES)
#define MALTA_CARTFLASH_BYTES          MIPS_SIZE_2M
#endif
