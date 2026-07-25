/*
 * VM-backed physically contiguous dumb framebuffer storage.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/drm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vmspace.h>

int
drm_framebuffer_alloc_contiguous(struct drm_framebuffer *fb,
    unsigned max_address, unsigned alignment)
{
    struct vm_page_request request;
    struct vm_page *pages;
    vm_size_t mapped;
    vm_pfn_t index;
    int error;

    if (fb == 0 || fb->bytes == 0 || fb->pages != 0 || fb->npages != 0 ||
        vm_size_round_page(fb->bytes, &mapped) != 0)
        return EINVAL;
    if (alignment < VM_PAGE_SIZE)
        alignment = VM_PAGE_SIZE;

    vm_page_request_init(&request);
    request.vpr_npages = mapped / VM_PAGE_SIZE;
    request.vpr_alignment = alignment;
    request.vpr_max_address = max_address;
    request.vpr_state = VM_PAGE_WIRED;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &pages);
    if (error != 0)
        return error;
    error = vm_page_device_claim(&vm_page_boot_allocator, pages,
        request.vpr_npages);
    if (error != 0) {
        for (index = 0; index < request.vpr_npages; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                pages + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, pages,
            request.vpr_npages);
        return error;
    }

    fb->vaddr = (volatile unsigned char *)pmap_page_direct_map(pages,
        PMAP_CACHE_UNCACHED);
    if (fb->vaddr == 0) {
        (void)vm_page_device_release(&vm_page_boot_allocator, pages,
            request.vpr_npages);
        for (index = 0; index < request.vpr_npages; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                pages + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, pages,
            request.vpr_npages);
        return ENXIO;
    }
    fb->paddr = pages->vmp_paddr;
    fb->reserved_bytes = mapped;
    fb->cache_mode = PMAP_CACHE_UNCACHED;
    fb->pages = pages;
    fb->npages = request.vpr_npages;
    return 0;
}

void
drm_framebuffer_free_contiguous(struct drm_framebuffer *fb)
{
    struct vm_page *pages;
    vm_pfn_t npages;
    vm_pfn_t index;
    int error;

    if (fb == 0 || fb->pages == 0 || fb->npages == 0)
        return;
    pages = fb->pages;
    npages = fb->npages;
    error = vmspace_revoke_device(fb->paddr, fb->reserved_bytes);
    if (error != 0) {
        printf("drm: cannot revoke framebuffer mappings, error=%d\n",
            error);
        return;
    }
    error = vm_page_device_release(&vm_page_boot_allocator, pages,
        npages);
    if (error != 0) {
        printf("drm: cannot release framebuffer pages, error=%d\n",
            error);
        return;
    }
    for (index = 0; index < npages; ++index) {
        error = vm_page_counter_dec(&vm_page_boot_allocator,
            pages + index, VM_PAGE_COUNTER_WIRE);
        if (error != 0) {
            printf("drm: cannot unwire framebuffer page, error=%d\n",
                error);
            return;
        }
    }
    error = vm_page_free(&vm_page_boot_allocator, pages, npages);
    if (error != 0) {
        printf("drm: cannot free framebuffer pages, error=%d\n",
            error);
        return;
    }
    fb->vaddr = 0;
    fb->paddr = 0;
    fb->reserved_bytes = 0;
    fb->pages = 0;
    fb->npages = 0;
}
