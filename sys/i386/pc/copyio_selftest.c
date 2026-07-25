#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <vm/vmspace.h>

#include "copyio.h"
#include "vmspace_bootstrap.h"

#define I386_COPYIO_TEST_VADDR  0x51000000u
#define I386_COPYIO_TEST_SIZE   (2u * VM_PAGE_SIZE)

static int
i386_copyio_string_equal(const char *left, const char *right)
{
    while (*left == *right) {
        if (*left == '\0')
            return 1;
        ++left;
        ++right;
    }
    return 0;
}

int
i386_copyio_selftest(void)
{
    static char kernel_string[] = "low-kernel-copykstr";
    struct vmspace *source;
    struct vmspace *child;
    vm_paddr_t source_first;
    vm_paddr_t source_second;
    vm_paddr_t child_first;
    vm_paddr_t child_second;
    vm_pfn_t free_before;
    vm_vaddr_t crossing;
    vm_vaddr_t string_address;
    uint32_t source_value;
    uint32_t child_value;
    uint32_t output;
    char copied_string[64];
    u_int copied;
    int error;

    source = (struct vmspace *)0;
    child = (struct vmspace *)0;
    source_value = 0x6c91e2d4u;
    child_value = 0xa57b3c18u;
    output = 0;
    crossing = I386_COPYIO_TEST_VADDR + VM_PAGE_SIZE - 2u;
    string_address = I386_COPYIO_TEST_VADDR + VM_PAGE_SIZE - 7u;
    free_before = vm_page_boot_allocator.vpa_free_count;

    copied = 0;
    if (copykstr((caddr_t)kernel_string, copied_string,
        sizeof(copied_string), &copied) != 0 ||
        copied != sizeof(kernel_string) ||
        !i386_copyio_string_equal(kernel_string, copied_string))
        return EFAULT;

    if (copyin((caddr_t)crossing, (caddr_t)&output, 0) != 0 ||
        copyout((caddr_t)&source_value, (caddr_t)crossing, 0) != 0)
        return EFAULT;
    if (copyin((caddr_t)crossing, (caddr_t)&output,
        sizeof(output)) != EFAULT)
        return EFAULT;

    error = vmspace_create(&source);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(source, I386_COPYIO_TEST_VADDR,
        I386_COPYIO_TEST_SIZE, VM_PROT_READ | VM_PROT_WRITE, 0);
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(source);
    if (error != 0)
        goto out;

    error = copyout((caddr_t)&source_value, (caddr_t)crossing,
        sizeof(source_value));
    if (error != 0)
        goto out;
    error = copyin((caddr_t)crossing, (caddr_t)&output, sizeof(output));
    if (error != 0 || output != source_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    if (copyin((caddr_t)(I386_COPYIO_TEST_VADDR +
        I386_COPYIO_TEST_SIZE), (caddr_t)&output, sizeof(output)) !=
        EFAULT ||
        copyout((caddr_t)&source_value,
        (caddr_t)(source->vms_map.vmm_max - 1u), 2) != EFAULT) {
        error = EFAULT;
        goto out;
    }

    error = vmspace_clone(source, &child);
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(child);
    if (error != 0)
        goto out;
    error = copyout((caddr_t)&child_value, (caddr_t)crossing,
        sizeof(child_value));
    if (error != 0)
        goto out;
    output = 0;
    error = copyin((caddr_t)crossing, (caddr_t)&output, sizeof(output));
    if (error != 0 || output != child_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }

    error = i386_vmspace_activate(source);
    if (error != 0)
        goto out;
    output = 0;
    error = copyin((caddr_t)crossing, (caddr_t)&output, sizeof(output));
    if (error != 0 || output != source_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_extract(source->vms_pmap,
        I386_COPYIO_TEST_VADDR, &source_first);
    if (error != 0)
        goto out;
    error = pmap_extract(source->vms_pmap,
        I386_COPYIO_TEST_VADDR + VM_PAGE_SIZE, &source_second);
    if (error != 0)
        goto out;
    error = pmap_extract(child->vms_pmap,
        I386_COPYIO_TEST_VADDR, &child_first);
    if (error != 0)
        goto out;
    error = pmap_extract(child->vms_pmap,
        I386_COPYIO_TEST_VADDR + VM_PAGE_SIZE, &child_second);
    if (error != 0 ||
        (source_first & ~VM_PAGE_MASK) ==
        (child_first & ~VM_PAGE_MASK) ||
        (source_second & ~VM_PAGE_MASK) ==
        (child_second & ~VM_PAGE_MASK)) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }

    error = copyout((caddr_t)kernel_string, (caddr_t)string_address,
        sizeof(kernel_string));
    if (error != 0)
        goto out;
    copied = 0;
    error = copyinstr((caddr_t)string_address, copied_string,
        sizeof(copied_string), &copied);
    if (error != 0 || copied != sizeof(kernel_string) ||
        !i386_copyio_string_equal(kernel_string, copied_string)) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    copied = 0;
    if (copyinstr((caddr_t)string_address, copied_string, 4,
        &copied) != ENOENT || copied != 4) {
        error = EFAULT;
        goto out;
    }
    if (copyinstr((caddr_t)(I386_COPYIO_TEST_VADDR +
        I386_COPYIO_TEST_SIZE), copied_string, sizeof(copied_string),
        &copied) != EFAULT) {
        error = EFAULT;
        goto out;
    }

    error = vmspace_protect(source, I386_COPYIO_TEST_VADDR,
        I386_COPYIO_TEST_SIZE, VM_PROT_READ);
    if (error != 0)
        goto out;
    if (copyout((caddr_t)&child_value, (caddr_t)crossing,
        sizeof(child_value)) != EFAULT) {
        error = EFAULT;
        goto out;
    }
    error = 0;

out:
    if (vmspace_current() != (struct vmspace *)0)
        i386_vmspace_deactivate(vmspace_current());
    if (source != (struct vmspace *)0) {
        if (vmspace_destroy(source) != 0 && error == 0)
            error = EFAULT;
    }
    if (child != (struct vmspace *)0) {
        if (vmspace_destroy(child) != 0 && error == 0)
            error = EFAULT;
    }
    if (vm_page_boot_allocator.vpa_free_count != free_before &&
        error == 0)
        error = EFAULT;
    return error;
}
