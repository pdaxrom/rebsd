/* MIPS direct-map support for bootstrap vm_page metadata and diagnostics. */

#include <sys/types.h>
#include <sys/errno.h>
#include <vm/vm_page.h>

#define MIPS_VM_KSEG0_BASE          0x80000000u
#define MIPS_VM_DIRECT_PHYS_MASK    0x1fffffffu

void *
vm_page_md_direct_map(vm_paddr_t paddr, vm_size_t size)
{
    if (size == 0 || paddr > MIPS_VM_DIRECT_PHYS_MASK ||
        size - 1 > MIPS_VM_DIRECT_PHYS_MASK - paddr)
        return 0;
    return (void *)(uintptr_t)(MIPS_VM_KSEG0_BASE | paddr);
}

int
vm_page_md_poison(void *arg, vm_paddr_t paddr, uint8_t pattern, int verify)
{
    volatile uint8_t *memory;
    vm_size_t i;

    (void)arg;
    memory = (volatile uint8_t *)vm_page_md_direct_map(paddr,
        VM_PAGE_SIZE);
    if (memory == 0)
        return EFAULT;
    if (verify) {
        for (i = 0; i < VM_PAGE_SIZE; ++i) {
            if (memory[i] != pattern)
                return EFAULT;
        }
    } else {
        for (i = 0; i < VM_PAGE_SIZE; ++i)
            memory[i] = pattern;
    }
    return 0;
}
