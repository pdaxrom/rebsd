#ifndef _CMD_VM_MEMORY_H_
#define _CMD_VM_MEMORY_H_

struct vm_memory_info {
    long vmi_phys_kb;
    long vmi_total_kb;
    long vmi_used_kb;
    long vmi_free_kb;
    long vmi_active_kb;
};

int vm_memory_read(struct vm_memory_info *info);

#endif
