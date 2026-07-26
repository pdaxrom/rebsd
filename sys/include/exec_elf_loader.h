#ifndef _SYS_EXEC_ELF_LOADER_H_
#define _SYS_EXEC_ELF_LOADER_H_

#ifdef KERNEL

#include <vm/vm_map.h>

#define EXEC_ELF_MAX_LOAD_SEGMENTS (VM_MAP_MAX_ENTRIES - 1)

struct elf_ehdr;
struct vmspace;

struct exec_elf_segment {
    vm_vaddr_t ees_start;
    vm_size_t  ees_size;
    vm_prot_t  ees_protection;
};

struct exec_elf_image {
    vm_vaddr_t eei_entry;
    unsigned   eei_segment_count;
    struct exec_elf_segment
        eei_segments[EXEC_ELF_MAX_LOAD_SEGMENTS];
};

int exec_elf_header_valid(const struct elf_ehdr *);
int exec_elf_image_load(struct vmspace *, const void *, vm_size_t,
    struct exec_elf_image *);
int exec_elf_image_unload(struct vmspace *, struct exec_elf_image *);

#endif

#endif
