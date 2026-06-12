#ifndef _N64_LAYOUT_H_
#define _N64_LAYOUT_H_

/*
 * Fixed first-stage N64 memory layout.
 *
 * 4 MiB system:
 *   0x00000000..0x000fffff  kernel, vectors, u areas
 *   0x00100000..0x002fffff  wired kuseg user window
 *   0x00380000..0x003fffff  RAM swap fallback
 *
 * 8 MiB system:
 *   0x00000000..0x000fffff  kernel, vectors, u areas
 *   0x00100000..0x002fffff  wired kuseg user window
 *   0x00400000..0x007fffff  Expansion Pak RAM swap
 */
#define N64_SIZE_512K                  0x00080000
#define N64_SIZE_1M                    0x00100000
#define N64_SIZE_2M                    0x00200000
#define N64_SIZE_4M                    0x00400000
#define N64_SIZE_8M                    0x00800000

#define N64_PHYS_RDRAM_BASE            0x00000000
#define N64_KSEG0_BASE                 0x80000000
#define N64_KSEG1_BASE                 0xa0000000
#define N64_KSEG_ADDR_MASK             0xe0000000
#define N64_KSEG_PHYS_MASK             0x1fffffff
#define N64_RDRAM_BASE                 N64_KSEG0_BASE
#define N64_RDRAM_BASE_UNCACHED        N64_KSEG1_BASE

#define N64_RDRAM_SIZE_4M              N64_SIZE_4M
#define N64_RDRAM_SIZE_8M              N64_SIZE_8M
#define N64_BASE_RDRAM_SIZE            N64_RDRAM_SIZE_4M

#define N64_KERNEL_PHYS_BASE           N64_PHYS_RDRAM_BASE
#define N64_KERNEL_VADDR_BASE          N64_KSEG0_BASE
#define N64_KERNEL_LOAD_VADDR          0x80001000
#define N64_KERNEL_RESERVED            N64_SIZE_1M
#define N64_KERNEL_LINK_LENGTH         0x000f0000
#define N64_KERNEL_DATA_START          N64_KERNEL_VADDR_BASE
#define N64_KERNEL_DATA_END            (N64_KERNEL_DATA_START + N64_KERNEL_RESERVED)
#define N64_UAREA_SIZE                 0x00001000
#define N64_U0AREA_VADDR               0x800f0000
#define N64_UAREA_VADDR                0x800f1000

#define N64_USER_VADDR_START           0x00400000
#define N64_USER_PHYS_START            (N64_KERNEL_PHYS_BASE + N64_KERNEL_RESERVED)
#define N64_USER_TLB_PAGE_SIZE         N64_SIZE_1M
#define N64_USER_TLB_PAIR_SIZE         (2 * N64_USER_TLB_PAGE_SIZE)
#define N64_USER_MAXMEM                N64_USER_TLB_PAIR_SIZE
#define N64_USER_VADDR_END             (N64_USER_VADDR_START + N64_USER_MAXMEM)
#define N64_USER_GP_OFFSET             0x00007ff0

#define N64_BASE_SWAP_BYTES            N64_SIZE_512K
#define N64_BASE_SWAP_PHYS_START       (N64_BASE_RDRAM_SIZE - N64_BASE_SWAP_BYTES)
#define N64_EXPANSION_SWAP_PHYS_START  N64_BASE_RDRAM_SIZE

#endif
