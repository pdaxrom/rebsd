#ifndef _CI20_LAYOUT_H_
#define _CI20_LAYOUT_H_

#define CI20_SIZE_512K                 0x00080000
#define CI20_SIZE_1M                   0x00100000
#define CI20_SIZE_2M                   0x00200000
#define CI20_SIZE_4M                   0x00400000
#define CI20_SIZE_8M                   0x00800000
#define CI20_SIZE_16M                  0x01000000
#define CI20_SIZE_32M                  0x02000000
#define CI20_SIZE_256M                 0x10000000

#define MIPS_KSEG0_BASE                0x80000000
#define MIPS_KSEG1_BASE                0xa0000000
#define MIPS_KSEG_PHYS_MASK            0x1fffffff
#define MIPS_PHYS_TO_KSEG0(x)          ((void *)(MIPS_KSEG0_BASE | (unsigned)(x)))
#define MIPS_PHYS_TO_KSEG1(x)          ((void *)(MIPS_KSEG1_BASE | (unsigned)(x)))
#define MIPS_KSEG_TO_PHYS(x)           ((unsigned)(x) & MIPS_KSEG_PHYS_MASK)

/*
 * First Ci20 layout.  Only the first low-memory bank is used.
 *
 *   0x00000000..0x000fffff  vectors/unused low RAM
 *   0x00100000..0x002fbfff  kernel ELF
 *   0x002fc000..0x002fffff  fixed u0/u areas
 *   0x00300000..0x006fffff  wired kuseg user window
 *   0x00700000..0x007fffff  /var RAM disk
 *   0x00800000..             linked root filesystem
 *                             followed by optional RAM-backed swap
 */
#define CI20_PHYS_RAM_BASE             0x00000000
#ifdef CI20_RAM_SIZE_OVERRIDE
#define CI20_RAM_SIZE                  CI20_RAM_SIZE_OVERRIDE
#else
#define CI20_RAM_SIZE                  CI20_SIZE_256M
#endif
#define CI20_CPU_KHZ                   1200000u
#define MIPS_COUNT_KHZ                 600000u
#define CI20_KERNEL_LOAD_VADDR         0x80100000
#define CI20_KERNEL_LINK_LENGTH        CI20_SIZE_2M
#define CI20_KERNEL_DATA_START         MIPS_KSEG0_BASE
#define CI20_KERNEL_DATA_END           (MIPS_KSEG0_BASE + 0x00300000)
#define CI20_UAREA_SIZE                0x00002000
#define CI20_U0AREA_VADDR              0x802fc000
#define CI20_UAREA_VADDR               (CI20_U0AREA_VADDR + CI20_UAREA_SIZE)

#define MIPS_USER_VADDR_START          0x00400000
#define MIPS_USER_PHYS_START           0x00300000
#define MIPS_USER_TLB_PAGE_SIZE        CI20_SIZE_1M
#define MIPS_USER_TLB_PAIR_SIZE        (2 * MIPS_USER_TLB_PAGE_SIZE)
#define MIPS_USER_TLB_PAIRS            2
#define MIPS_USER_MAXMEM               (MIPS_USER_TLB_PAIRS * \
                                         MIPS_USER_TLB_PAIR_SIZE)
#define MIPS_USER_VADDR_END            (MIPS_USER_VADDR_START + MIPS_USER_MAXMEM)
#define MIPS_USER_GP_OFFSET            0x00007ff0

#define CI20_RAMDISK_VAR_PHYS_START    0x00700000
#define CI20_RAMDISK_VAR_BYTES         CI20_SIZE_1M
#define CI20_ROMDISK_PHYS_START        0x00800000
#ifdef CI20_ROMDISK_BYTES_OVERRIDE
#define CI20_ROMDISK_BYTES             CI20_ROMDISK_BYTES_OVERRIDE
#else
#define CI20_ROMDISK_BYTES             CI20_SIZE_16M
#endif
#define CI20_RAMSWAP_PHYS_START        (CI20_ROMDISK_PHYS_START + \
                                         CI20_ROMDISK_BYTES)
#define CI20_RAMSWAP_MAX_BYTES         (CI20_RAM_SIZE > CI20_RAMSWAP_PHYS_START ? \
                                         CI20_RAM_SIZE - CI20_RAMSWAP_PHYS_START : 0)
#ifdef CI20_RAMSWAP_BYTES_OVERRIDE
#define CI20_RAMSWAP_BYTES             (CI20_RAMSWAP_BYTES_OVERRIDE < \
                                         CI20_RAMSWAP_MAX_BYTES ? \
                                         CI20_RAMSWAP_BYTES_OVERRIDE : \
                                         CI20_RAMSWAP_MAX_BYTES)
#else
#define CI20_RAMSWAP_BYTES             CI20_RAMSWAP_MAX_BYTES
#endif

#endif
