#ifndef _SYS_EXEC_ELF_LOADER_H_
#define _SYS_EXEC_ELF_LOADER_H_

#ifdef KERNEL

#include <vm/vm_map.h>

#define EXEC_ELF_MAX_LOAD_SEGMENTS (VM_MAP_MAX_ENTRIES - 1)

struct elf_ehdr;
struct elf_phdr;
struct vmspace;

struct exec_elf_load_segment {
    vm_vaddr_t eels_map_start;
    vm_size_t  eels_map_size;
    vm_vaddr_t eels_vaddr;
    vm_size_t  eels_file_size;
    vm_size_t  eels_memory_size;
    vm_size_t  eels_file_offset;
    vm_prot_t  eels_protection;
};

struct exec_elf_load {
    vm_vaddr_t eel_entry;
    unsigned   eel_segment_count;
    struct exec_elf_load_segment
        eel_segments[EXEC_ELF_MAX_LOAD_SEGMENTS];
};

int exec_elf_header_valid(const struct elf_ehdr *);
int exec_elf_load_plan(const struct elf_ehdr *, const struct elf_phdr *,
    vm_size_t, struct exec_elf_load *);
int exec_elf_load_map(struct vmspace *, const struct exec_elf_load *);
int exec_elf_load_finish(struct vmspace *, const struct exec_elf_load *);

#endif

#endif
