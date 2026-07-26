#ifndef _N64_LAYOUT_H_
#define _N64_LAYOUT_H_

/*
 * Fixed first-stage N64 memory layout.
 *
 * 4 MiB system:
 *   0x00000000..0x000fffff  kernel, vectors, bootstrap u area
 *   0x00100000..0x002fffff  VM page pool after bootstrap
 *   0x00300000..0x0033ffff  resident stage0/restart image
 *   0x00340000..0x0037ffff  stage0/320x240x16 framebuffer alias
 *   0x00380000..0x003fffff  RAM swap fallback
 *
 * 8 MiB system:
 *   0x00000000..0x000fffff  kernel, vectors, bootstrap u area
 *   0x00100000..0x002fffff  VM page pool after bootstrap
 *   0x00300000..0x0037ffff  resident stage0/restart image
 *   0x00380000..0x004fffff  VM page pool after bootstrap
 *   0x00500000..0x005fffff  /var RAM disk
 *   0x00600000..0x007fffff  Expansion Pak RAM swap store
 *
 * N64 VI framebuffers are contiguous wired VM allocations and therefore do
 * not occupy a fixed Expansion Pak reserve.
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
#define N64_KERNEL_LINK_LENGTH         0x000fd000
#define N64_KERNEL_DATA_START          N64_KERNEL_VADDR_BASE
#define N64_KERNEL_DATA_END            (N64_KERNEL_DATA_START + N64_KERNEL_RESERVED)
#define N64_UAREA_SIZE                 0x00002000
#define N64_UAREA_VADDR                0x800fe000

#define N64_STAGE0_VADDR               0x80300000
#define N64_STAGE0_PHYS_START          0x00300000
#ifndef N64_STAGE0_RESERVED_BYTES
#define N64_STAGE0_RESERVED_BYTES      N64_SIZE_512K
#endif
#define N64_STAGE0_PHYS_END            (N64_STAGE0_PHYS_START + \
                                         N64_STAGE0_RESERVED_BYTES)

#define N64_USER_VADDR_START           0x00400000
#define N64_USER_PHYS_START            (N64_KERNEL_PHYS_BASE + N64_KERNEL_RESERVED)
#define N64_USER_TLB_PAGE_SIZE         N64_SIZE_1M
#define N64_USER_TLB_PAIR_SIZE         (2 * N64_USER_TLB_PAGE_SIZE)
#define N64_USER_TLB_PAIRS_4M          1
#define N64_USER_TLB_PAIRS_8M          2
#define N64_USER_TLB_PAIRS             N64_USER_TLB_PAIRS_8M
#define N64_USER_MAXMEM_4M             (N64_USER_TLB_PAIRS_4M * N64_USER_TLB_PAIR_SIZE)
#define N64_USER_MAXMEM_8M             (N64_USER_TLB_PAIRS_8M * N64_USER_TLB_PAIR_SIZE)
#define N64_USER_MAXMEM                N64_USER_MAXMEM_8M
#define N64_USER_VADDR_END_4M          (N64_USER_VADDR_START + N64_USER_MAXMEM_4M)
#define N64_USER_VADDR_END_8M          (N64_USER_VADDR_START + N64_USER_MAXMEM_8M)
#define N64_USER_VADDR_END             N64_USER_VADDR_END_8M
#define N64_USER_PHYS_END_4M           (N64_USER_PHYS_START + N64_USER_MAXMEM_4M)
#define N64_USER_PHYS_END_8M           (N64_USER_PHYS_START + N64_USER_MAXMEM_8M)
#define N64_USER_PHYS_END              N64_USER_PHYS_END_8M
#define N64_USER_GP_OFFSET             0x00007ff0
#define N64_FB_USER_VADDR_START        N64_USER_VADDR_END

#define N64_BASE_SWAP_BYTES            N64_SIZE_512K
#define N64_BASE_SWAP_PHYS_START       (N64_BASE_RDRAM_SIZE - N64_BASE_SWAP_BYTES)
#define N64_RAMDISK_4M_VAR_BYTES       0x00020000
#define N64_RAMDISK_8M_VAR_BYTES       0x00100000

#define N64_VIDEO_BPP_BYTES            2
#define N64_VIDEO_320_WIDTH            320
#define N64_VIDEO_320_HEIGHT           240
#define N64_VIDEO_640_WIDTH            640
#define N64_VIDEO_640_HEIGHT           480
#define N64_VIDEO_320_BYTES            (N64_VIDEO_320_WIDTH * N64_VIDEO_320_HEIGHT * N64_VIDEO_BPP_BYTES)
#define N64_VIDEO_640_BYTES            (N64_VIDEO_640_WIDTH * N64_VIDEO_640_HEIGHT * N64_VIDEO_BPP_BYTES)
#define N64_VIDEO_TLB_PAGE_SIZE        0x00010000
#define N64_VIDEO_TLB_PAIR_SIZE        (2 * N64_VIDEO_TLB_PAGE_SIZE)
#define N64_VIDEO_MAP_ROUND(bytes)     (((bytes) + N64_VIDEO_TLB_PAIR_SIZE - 1) & ~(N64_VIDEO_TLB_PAIR_SIZE - 1))
#define N64_VIDEO_320_MAP_BYTES        N64_VIDEO_MAP_ROUND(N64_VIDEO_320_BYTES)
#define N64_VIDEO_640_MAP_BYTES        N64_VIDEO_MAP_ROUND(N64_VIDEO_640_BYTES)
#define N64_BASE_FB_RESERVED_BYTES     N64_VIDEO_320_MAP_BYTES
#define N64_BASE_FB_PHYS_START         (N64_BASE_SWAP_PHYS_START - N64_BASE_FB_RESERVED_BYTES)
#define N64_EXPANSION_FB_PHYS_START    N64_USER_PHYS_END_8M
#define N64_EXPANSION_FB_RESERVED_BYTES 0
#define N64_EXPANSION_SWAP_PHYS_START  (N64_EXPANSION_FB_PHYS_START + N64_EXPANSION_FB_RESERVED_BYTES)

#endif
