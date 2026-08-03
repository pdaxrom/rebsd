/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/dma.h>
#include <sys/systm.h>
#include <machine/layout.h>

#ifndef CI20_DMA_POOL_BYTES
#define CI20_DMA_POOL_BYTES     (256u * 1024u)
#endif

#define CI20_DMA_POOL_ALIGN     4096u

static unsigned char ci20_dma_storage[CI20_DMA_POOL_BYTES]
    __attribute__((section(".dma"), aligned(CI20_DMA_POOL_ALIGN)));

static void
ci20_dma_barrier(const struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction)
{
    (void)mem;
    (void)offset;
    (void)length;
    (void)direction;
    asm volatile ("sync" ::: "memory");
}

static const struct dma_backend_ops ci20_dma_ops = {
    ci20_dma_barrier,
    ci20_dma_barrier,
    0,
    0,
    0,
    0,
};

void
dmaattach(int unit)
{
    dma_addr_t paddr;
    void *vaddr;
    int error;

    (void)unit;
    paddr = MIPS_KSEG_TO_PHYS(ci20_dma_storage);
    vaddr = MIPS_PHYS_TO_KSEG1(paddr);
    error = dma_pool_init(vaddr, paddr, sizeof(ci20_dma_storage),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, &ci20_dma_ops);
    if (error != 0) {
        printf("dma: Ci20 pool initialization failed, error=%d\n", error);
        return;
    }
    printf("dma: Ci20 uncached pool phys=%x size=%u align=%u\n",
        paddr, (unsigned)sizeof(ci20_dma_storage), CI20_DMA_POOL_ALIGN);
}
