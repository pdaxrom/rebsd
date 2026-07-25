#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <vm/vmspace.h>

static int
i386_copyio_user_range(const struct vmspace *vmspace,
    vm_vaddr_t address, u_int size)
{
    if (address < vmspace->vms_map.vmm_min ||
        address >= vmspace->vms_map.vmm_max ||
        size > vmspace->vms_map.vmm_max - address)
        return EFAULT;
    return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
    struct vmspace *vmspace;
    vm_vaddr_t address;

    if (nbytes == 0)
        return 0;
    address = (vm_vaddr_t)(unsigned long)to;
    vmspace = vmspace_current();
    if (vmspace == (struct vmspace *)0 ||
        i386_copyio_user_range(vmspace, address, nbytes) != 0)
        return EFAULT;
    /* Transfer through the direct map; never dereference the user VA. */
    return vmspace_write_context(vmspace, address, from, nbytes,
        VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
    struct vmspace *vmspace;
    vm_vaddr_t address;

    if (nbytes == 0)
        return 0;
    address = (vm_vaddr_t)(unsigned long)from;
    vmspace = vmspace_current();
    if (vmspace == (struct vmspace *)0 ||
        i386_copyio_user_range(vmspace, address, nbytes) != 0)
        return EFAULT;
    /* The early caller is process context; IRQ use will omit CAN_SLEEP. */
    return vmspace_read_context(vmspace, address, to, nbytes,
        VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}
