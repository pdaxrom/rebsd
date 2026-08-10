#ifndef _CI20_LAYOUT_H_
#define _CI20_LAYOUT_H_

#define CI20_SIZE_512K                 0x00080000
#define CI20_SIZE_1M                   0x00100000
#define CI20_SIZE_2M                   0x00200000
#define CI20_SIZE_4M                   0x00400000
#define CI20_SIZE_8M                   0x00800000
#define CI20_SIZE_16M                  0x01000000
#define CI20_SIZE_32M                  0x02000000
#define CI20_SIZE_64M                  0x04000000
#define CI20_SIZE_256M                 0x10000000
#define CI20_SIZE_1G                   0x40000000

#define MIPS_KSEG0_BASE                0x80000000
#define MIPS_KSEG1_BASE                0xa0000000
#define MIPS_KSEG_PHYS_MASK            0x1fffffff
#define MIPS_PHYS_TO_KSEG0(x)          ((void *)(MIPS_KSEG0_BASE | (unsigned)(x)))
#define MIPS_PHYS_TO_KSEG1(x)          ((void *)(MIPS_KSEG1_BASE | (unsigned)(x)))
#define MIPS_KSEG_TO_PHYS(x)           ((unsigned)(x) & MIPS_KSEG_PHYS_MASK)

/*
 * Ci20 has two physical RAM banks separated by an address hole:
 *
 *   0x00000000..0x0fffffff  256 MiB low bank
 *   0x30000000..0x5fffffff  768 MiB high bank
 *
 *   0x00000000..0x000fffff  vectors/unused low RAM
 *   0x00100000..0x002fbfff  kernel ELF
 *   0x002fc000..0x002fdfff  free after removal of historical u0
 *   0x002fe000..0x002fffff  proc0 bootstrap u area/kernel stack
 *   0x00300000..0x006fffff  legacy user-window reserve (unwired after proc1)
 *   0x00700000..0x007fffff  general-purpose RAM
 *   0x00800000..             linked root filesystem
 *   0x0f800000..0x0fffffff  HDMI framebuffer reserve
 */
#define CI20_PHYS_RAM_BASE             0x00000000
#define CI20_LOW_RAM_BYTES             CI20_SIZE_256M
#define CI20_HIGH_RAM_PHYS_START       0x30000000
#define CI20_HIGH_RAM_MAX_BYTES        0x30000000
#define CI20_HIGH_RAM_VADDR_START      0xc0000000
#define CI20_HIGH_RAM_VADDR_END        0xf0000000
#ifdef CI20_RAM_SIZE_OVERRIDE
#define CI20_RAM_SIZE                  CI20_RAM_SIZE_OVERRIDE
#else
#define CI20_RAM_SIZE                  CI20_SIZE_1G
#endif
#define CI20_HIGH_RAM_BYTES            (CI20_RAM_SIZE > CI20_LOW_RAM_BYTES ? \
                                         CI20_RAM_SIZE - CI20_LOW_RAM_BYTES : 0)
#define CI20_CPU_KHZ                   1200000u
#define MIPS_COUNT_KHZ                 600000u
#define CI20_KERNEL_LOAD_VADDR         0x80100000
#define CI20_KERNEL_LINK_LENGTH        CI20_SIZE_2M
#define CI20_KERNEL_DATA_START         MIPS_KSEG0_BASE
#define CI20_KERNEL_DATA_END           (MIPS_KSEG0_BASE + 0x00300000)
#define CI20_UAREA_SIZE                0x00002000
#define CI20_UAREA_VADDR               0x802fe000

#define MIPS_USER_VADDR_START          0x00400000
#define MIPS_USER_PHYS_START           0x00300000
#define MIPS_USER_TLB_PAGE_SIZE        CI20_SIZE_1M
#define MIPS_USER_TLB_PAIR_SIZE        (2 * MIPS_USER_TLB_PAGE_SIZE)
#define MIPS_USER_TLB_PAIRS            2
#define MIPS_LEGACY_USER_BYTES         (MIPS_USER_TLB_PAIRS * \
                                         MIPS_USER_TLB_PAIR_SIZE)
#define CI20_HIGH_TLB_PAGE_SIZE        CI20_SIZE_16M
#define CI20_HIGH_TLB_PAIR_SIZE        (2 * CI20_HIGH_TLB_PAGE_SIZE)
#define CI20_HIGH_TLB_INDEX            MIPS_USER_TLB_PAIRS
#define CI20_HIGH_TLB_ENTRIES          \
                                        ((CI20_HIGH_RAM_BYTES + \
                                          CI20_HIGH_TLB_PAIR_SIZE - 1) / \
                                         CI20_HIGH_TLB_PAIR_SIZE)
#define CI20_WIRED_ENTRIES             (MIPS_USER_TLB_PAIRS + \
                                         CI20_HIGH_TLB_ENTRIES)
#define MIPS_USER_MAXMEM               CI20_SIZE_64M
#define MIPS_USER_VADDR_END            (MIPS_USER_VADDR_START + MIPS_USER_MAXMEM)
#define MIPS_USER_GP_OFFSET            0x00007ff0

#define CI20_ROMDISK_PHYS_START        0x00800000
#define CI20_FRAMEBUFFER_PHYS_START    0x0f800000
#define CI20_FRAMEBUFFER_BYTES         CI20_SIZE_8M
#ifdef CI20_ROMDISK_BYTES_OVERRIDE
#define CI20_ROMDISK_BYTES             CI20_ROMDISK_BYTES_OVERRIDE
#else
#define CI20_ROMDISK_BYTES             CI20_SIZE_16M
#endif
#endif
