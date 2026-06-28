#ifndef _MIPS_LAYOUT_H_
#define _MIPS_LAYOUT_H_

#define MIPS_SIZE_512K                 0x00080000
#define MIPS_SIZE_1M                   0x00100000
#define MIPS_SIZE_2M                   0x00200000
#define MIPS_SIZE_4M                   0x00400000
#define MIPS_SIZE_8M                   0x00800000
#define MIPS_SIZE_16M                  0x01000000
#define MIPS_SIZE_32M                  0x02000000

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
 *   0x002fc000..0x002fffff  fixed u0/u areas
 *   0x00300000..0x004fffff  wired kuseg user window
 *   0x00500000..0x005fffff  /var ramdisk
 *   0x00600000..0x015fffff  root filesystem loaded by QEMU
 *   0x01600000..0x01ffffff  RAM swap
 */
#define MALTA_PHYS_RAM_BASE            0x00000000
#define MALTA_RAM_SIZE                 MIPS_SIZE_32M
#define MALTA_CPU_KHZ                  100000u
#define MIPS_COUNT_KHZ                 50000u
#define MALTA_KERNEL_LOAD_VADDR        0x80100000
#define MALTA_KERNEL_LINK_LENGTH       MIPS_SIZE_2M
#define MALTA_KERNEL_DATA_START        MIPS_KSEG0_BASE
#define MALTA_KERNEL_DATA_END          (MIPS_KSEG0_BASE + 0x00300000)
#define MALTA_UAREA_SIZE               0x00002000
#define MALTA_U0AREA_VADDR             0x802fc000
#define MALTA_UAREA_VADDR              (MALTA_U0AREA_VADDR + MALTA_UAREA_SIZE)

#define MIPS_USER_VADDR_START          0x00400000
#define MIPS_USER_PHYS_START           0x00300000
#define MIPS_USER_TLB_PAGE_SIZE        MIPS_SIZE_1M
#define MIPS_USER_TLB_PAIR_SIZE        (2 * MIPS_USER_TLB_PAGE_SIZE)
#define MIPS_USER_MAXMEM               MIPS_USER_TLB_PAIR_SIZE
#define MIPS_USER_VADDR_END            (MIPS_USER_VADDR_START + MIPS_USER_MAXMEM)

#define MALTA_RAMDISK_VAR_PHYS_START   0x00500000
#define MALTA_RAMDISK_VAR_BYTES        MIPS_SIZE_1M
#define MALTA_ROMDISK_PHYS_START       0x00600000
#define MALTA_ROMDISK_BYTES            MIPS_SIZE_16M
#define MALTA_RAMSWAP_PHYS_START       (MALTA_ROMDISK_PHYS_START + \
                                         MALTA_ROMDISK_BYTES)
#define MALTA_RAMSWAP_BYTES            (MALTA_RAM_SIZE - MALTA_RAMSWAP_PHYS_START)

#endif
