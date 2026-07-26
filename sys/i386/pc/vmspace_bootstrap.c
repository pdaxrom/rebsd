#include <sys/param.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "vmspace_bootstrap.h"

#define I386_VMSPACE_TEST_VADDR  0x50000000u
#define I386_VMSPACE_TEST_SIZE   (2u * VM_PAGE_SIZE)

static struct vmspace *i386_vmspace_active;

struct vmspace *
vmspace_current(void)
{
    if (md_curuser != (struct user *)0 &&
        md_curuser->u_procp != (struct proc *)0)
        return md_curuser->u_procp->p_vmspace;
    return i386_vmspace_active;
}

int
i386_vmspace_activate(struct vmspace *vmspace)
{
    int error;

    error = vmspace_activate(vmspace);
    if (error == 0)
        i386_vmspace_active = vmspace;
    return error;
}

void
i386_vmspace_deactivate(struct vmspace *vmspace)
{
    if (i386_vmspace_active != vmspace)
        return;
    pmap_deactivate(vmspace->vms_pmap);
    i386_vmspace_active = (struct vmspace *)0;
}

int
i386_vmspace_fault_active(unsigned address, unsigned access, int user)
{
    struct vmspace *vmspace;
    int error;
    unsigned context;

    vmspace = vmspace_current();
    if (vmspace == (struct vmspace *)0)
        return ENOENT;
    error = pmap_fault_active((vm_vaddr_t)address, (vm_prot_t)access,
        user);
    if (error == 0)
        return 0;
    context = user ? VM_FAULT_USER : VM_FAULT_KERNEL;
    return vmspace_fault_context(vmspace, (vm_vaddr_t)address,
        (vm_prot_t)access,
        context | VM_FAULT_CAN_SLEEP);
}

int
i386_vmspace_bootstrap_selftest(void)
{
    struct vmspace *source;
    struct vmspace *child;
    volatile uint32_t *mapped;
    vm_paddr_t source_paddr;
    vm_paddr_t child_paddr;
    vm_pfn_t free_before;
    uint32_t source_value;
    uint32_t child_value;
    uint32_t output;
    int resident;
    int error;

    source = (struct vmspace *)0;
    child = (struct vmspace *)0;
    source_value = 0x11223344u;
    child_value = 0xa5a55a5au;
    output = 0;
    free_before = vm_page_boot_allocator.vpa_free_count;

    error = vmspace_create(&source);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(source, I386_VMSPACE_TEST_VADDR,
        I386_VMSPACE_TEST_SIZE, VM_PROT_READ | VM_PROT_WRITE, 0);
    if (error != 0)
        goto out;
    error = vmspace_fault(source, I386_VMSPACE_TEST_VADDR,
        VM_PROT_WRITE);
    if (error != 0)
        goto out;
    error = vmspace_write(source, I386_VMSPACE_TEST_VADDR + 37u,
        &source_value, sizeof(source_value));
    if (error != 0)
        goto out;
    error = vmspace_read(source, I386_VMSPACE_TEST_VADDR + 37u,
        &output, sizeof(output));
    if (error != 0 || output != source_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = i386_vmspace_activate(source);
    if (error != 0)
        goto out;
    mapped = (volatile uint32_t *)(I386_VMSPACE_TEST_VADDR + 36u);
    if ((*mapped & 0xffffff00u) !=
        ((source_value << 8) & 0xffffff00u)) {
        error = EFAULT;
        goto out;
    }
    *(volatile uint32_t *)(I386_VMSPACE_TEST_VADDR +
        VM_PAGE_SIZE + 13u) = 0x5a5aa5a5u;
    output = 0;
    error = vmspace_read(source, I386_VMSPACE_TEST_VADDR +
        VM_PAGE_SIZE + 13u, &output, sizeof(output));
    if (error != 0 || output != 0x5a5aa5a5u) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }

    error = vmspace_clone(source, &child);
    if (error != 0)
        goto out;
    error = vmspace_read(child, I386_VMSPACE_TEST_VADDR + 37u,
        &output, sizeof(output));
    if (error != 0 || output != source_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = vmspace_write(child, I386_VMSPACE_TEST_VADDR + 37u,
        &child_value, sizeof(child_value));
    if (error != 0)
        goto out;
    error = vmspace_read(source, I386_VMSPACE_TEST_VADDR + 37u,
        &output, sizeof(output));
    if (error != 0 || output != source_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = vmspace_read(child, I386_VMSPACE_TEST_VADDR + 37u,
        &output, sizeof(output));
    if (error != 0 || output != child_value) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_extract(source->vms_pmap, I386_VMSPACE_TEST_VADDR,
        &source_paddr);
    if (error != 0)
        goto out;
    error = pmap_extract(child->vms_pmap, I386_VMSPACE_TEST_VADDR,
        &child_paddr);
    if (error != 0 || source_paddr == child_paddr) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = i386_vmspace_activate(child);
    if (error != 0)
        goto out;
    output = *(volatile uint32_t *)(I386_VMSPACE_TEST_VADDR + 37u);
    if (output != child_value) {
        error = EFAULT;
        goto out;
    }
    error = vmspace_protect(source, I386_VMSPACE_TEST_VADDR,
        VM_PAGE_SIZE, VM_PROT_READ);
    if (error != 0)
        goto out;
    error = vmspace_write(source, I386_VMSPACE_TEST_VADDR + 37u,
        &child_value, sizeof(child_value));
    if (error == 0) {
        error = EFAULT;
        goto out;
    }
    error = vmspace_mincore(source, I386_VMSPACE_TEST_VADDR, &resident);
    if (error != 0 || !resident) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = vmspace_validate(source);
    if (error != 0)
        goto out;
    error = vmspace_validate(child);
    if (error != 0)
        goto out;
    error = 0;

out:
    if (i386_vmspace_active != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_vmspace_active);
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
