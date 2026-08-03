/*
 * i686 coherent DMA-pool attachment for the machine-independent allocator.
 */

#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/systm.h>

#define I386_DMA_POOL_BYTES     (512u * 1024u)
#define I386_DMA_POOL_ALIGN     4096u

static unsigned char i386_dma_storage[I386_DMA_POOL_BYTES]
    __attribute__((aligned(I386_DMA_POOL_ALIGN)));
static int i386_dma_attached;

int
i386_dma_attach(void)
{
    dma_addr_t paddr;
    int error;

    if (i386_dma_attached)
        return 0;
    paddr = (dma_addr_t)(unsigned long)i386_dma_storage;
    error = dma_pool_init(i386_dma_storage, paddr,
        sizeof(i386_dma_storage),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, 0);
    if (error != 0) {
        printf("dma: i686 pool initialization failed, error=%d\n",
            error);
        return error;
    }
    i386_dma_attached = 1;
    printf("dma: i686 coherent pool phys=%x size=%u align=%u\n",
        paddr, (unsigned)sizeof(i386_dma_storage),
        I386_DMA_POOL_ALIGN);
    return 0;
}

void
dmaattach(int unit)
{
    (void)unit;
    (void)i386_dma_attach();
}
