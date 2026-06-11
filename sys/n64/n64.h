#ifndef _N64_N64_H_
#define _N64_N64_H_

#define N64_PHYS_RDRAM_BASE     0x00000000u
#define N64_KSEG0_BASE          0x80000000u
#define N64_KSEG1_BASE          0xa0000000u
#define N64_RDRAM_BASE          0x80000000u
#define N64_RDRAM_BASE_UNCACHED 0xa0000000u

#define N64_RDRAM_SIZE_4M       (4u * 1024u * 1024u)
#define N64_RDRAM_SIZE_8M       (8u * 1024u * 1024u)
#define N64_RDRAM_THRESHOLD_4M  (N64_RDRAM_SIZE_4M - 0x40000u)
#define N64_RDRAM_THRESHOLD_8M  (N64_RDRAM_SIZE_8M - 0x40000u)
#define N64_BOOT_MEM_SIZE_ADDR  0x00000318u
#define N64_RDRAM_PROBE_4M      (N64_RDRAM_SIZE_4M - 16u)
#define N64_RDRAM_PROBE_8M      (N64_RDRAM_SIZE_8M - 16u)

#define N64_CPU_KHZ             93750u
#define N64_COUNT_KHZ           (N64_CPU_KHZ / 2u)

#define N64_PHYS_TO_KSEG0(x)    ((void *)(N64_KSEG0_BASE | (unsigned)(x)))
#define N64_PHYS_TO_KSEG1(x)    ((void *)(N64_KSEG1_BASE | (unsigned)(x)))

unsigned n64_rdram_size(void);

#endif
