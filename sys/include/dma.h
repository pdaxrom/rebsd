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

#ifndef _SYS_DMA_H_
#define _SYS_DMA_H_

#ifdef REBSD_DMA_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint32_t dma_addr_t;
#else
#include <sys/types.h>
typedef u_int dma_addr_t;
#endif

#ifndef DMA_MAX_ALLOCS
#define DMA_MAX_ALLOCS          32
#endif
#ifndef DMA_MAP_MAX_SEGMENTS
#define DMA_MAP_MAX_SEGMENTS    64
#endif

#define DMA_ZERO               0x0001u
#define DMA_32BIT              0x0002u
#define DMA_COHERENT           0x0004u
#define DMA_CONTIGUOUS         0x0008u
#define DMA_FLAG_MASK          0x000fu
#define DMA_CAPABILITY_MASK    (DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS)

enum dma_direction {
    DMA_TO_DEVICE = 0,
    DMA_FROM_DEVICE,
    DMA_BIDIRECTIONAL
};

struct vm_page;

struct dma_mem {
    void       *dm_vaddr;
    dma_addr_t  dm_paddr;
    size_t      dm_size;
    size_t      dm_align;
    unsigned    dm_flags;
    unsigned    dm_cookie;
};

struct dma_segment {
    dma_addr_t ds_addr;
    size_t ds_len;
};

/*
 * A loaded map describes one virtually contiguous buffer as bus-address
 * segments.  The final fields are storage for the machine-dependent
 * page pins; callers and device drivers must not inspect them.
 */
struct dma_map {
    struct dma_segment dm_segments[DMA_MAP_MAX_SEGMENTS];
    void       *dm_vaddr;
    size_t      dm_size;
    size_t      dm_max_segment_size;
    size_t      dm_boundary;
    dma_addr_t  dm_max_address;
    unsigned    dm_segment_count;
    unsigned    dm_max_segments;
    unsigned    dm_loaded;
    enum dma_direction dm_direction;
    void       *dm_backend_cookie;
    unsigned    dm_backend_page_count;
    struct vm_page *dm_backend_pages[DMA_MAP_MAX_SEGMENTS];
};

struct dma_backend_ops {
    void (*dbo_sync_for_device)(const struct dma_mem *, size_t, size_t,
        enum dma_direction);
    void (*dbo_sync_for_cpu)(const struct dma_mem *, size_t, size_t,
        enum dma_direction);
    int (*dbo_map_load)(struct dma_map *, void *, size_t,
        enum dma_direction);
    void (*dbo_map_unload)(struct dma_map *);
    void (*dbo_map_sync_for_device)(const struct dma_map *);
    void (*dbo_map_sync_for_cpu)(const struct dma_map *);
};

int dma_pool_init(void *vaddr, dma_addr_t paddr, size_t size,
    unsigned capabilities, const struct dma_backend_ops *ops);
int dma_pool_ready(void);
size_t dma_pool_size(void);
size_t dma_pool_available(void);

int dma_alloc(struct dma_mem *mem, size_t size, size_t alignment,
    unsigned flags);
int dma_free(struct dma_mem *mem);

int dma_sync_for_device(struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction);
int dma_sync_for_cpu(struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction);

int dma_map_load(struct dma_map *, void *, size_t, unsigned, size_t,
    size_t, dma_addr_t, enum dma_direction);
int dma_map_unload(struct dma_map *);
int dma_map_sync_for_device(struct dma_map *);
int dma_map_sync_for_cpu(struct dma_map *);

#endif /* _SYS_DMA_H_ */
