#include <sys/errno.h>
#include <sys/param.h>
#include <sys/exec_elf.h>
#include <sys/exec_elf_loader.h>
#include <sys/systm.h>
#include <vm/vmspace.h>

static int
exec_elf_machine_supported(unsigned machine)
{
    switch (machine) {
    ELF_MACHDEP_ID_CASES
    default:
        return 0;
    }
    return 1;
}

int
exec_elf_header_valid(const struct elf_ehdr *header)
{
    return header != 0 &&
        header->e_ident[EI_MAG0] == ELFMAG0 &&
        header->e_ident[EI_MAG1] == ELFMAG1 &&
        header->e_ident[EI_MAG2] == ELFMAG2 &&
        header->e_ident[EI_MAG3] == ELFMAG3 &&
        header->e_ident[EI_CLASS] == ELFCLASS32 &&
        header->e_ident[EI_DATA] == ELF_TARGET_DATA &&
        header->e_ident[EI_VERSION] == EV_CURRENT &&
        header->e_ident[EI_OSABI] == ELFOSABI_SYSV &&
        header->e_ident[EI_ABIVERSION] == 0 &&
        header->e_type == ET_EXEC &&
        exec_elf_machine_supported(header->e_machine) &&
        header->e_version == EV_CURRENT &&
        header->e_ehsize == sizeof(*header) &&
        header->e_phentsize == sizeof(struct elf_phdr) &&
        header->e_phoff != 0 && header->e_phnum != 0 &&
        header->e_phnum <= MAXBSIZE / sizeof(struct elf_phdr);
}

static int
exec_elf_protection(unsigned flags, vm_prot_t *protection)
{
    vm_prot_t value;

    if ((flags & ~(PF_R | PF_W | PF_X)) != 0 ||
        (flags & (PF_W | PF_X)) == (PF_W | PF_X))
        return ENOEXEC;
    value = VM_PROT_NONE;
    if ((flags & PF_R) != 0)
        value |= VM_PROT_READ;
    if ((flags & PF_W) != 0)
        value |= VM_PROT_WRITE;
    if ((flags & PF_X) != 0)
        value |= VM_PROT_EXECUTE;
    if (value == VM_PROT_NONE)
        return ENOEXEC;
    *protection = value;
    return 0;
}

static int
exec_elf_segment_overlap(const struct exec_elf_load *load,
    vm_vaddr_t start, vm_vaddr_t end)
{
    const struct exec_elf_load_segment *segment;
    unsigned index;
    vm_vaddr_t segment_end;

    for (index = 0; index < load->eel_segment_count; ++index) {
        segment = &load->eel_segments[index];
        segment_end = segment->eels_map_start + segment->eels_map_size;
        if (start < segment_end && segment->eels_map_start < end)
            return 1;
    }
    return 0;
}

int
exec_elf_load_plan(const struct elf_ehdr *header,
    const struct elf_phdr *phdrs,
    vm_size_t file_size, struct exec_elf_load *result)
{
    struct exec_elf_load candidate;
    const struct elf_phdr *phdr;
    struct exec_elf_load_segment *segment;
    vm_vaddr_t file_end;
    vm_vaddr_t segment_end;
    vm_vaddr_t map_end;
    vm_prot_t protection;
    unsigned index;
    int entry_found;

    if (header == (const struct elf_ehdr *)0 ||
        phdrs == (const struct elf_phdr *)0 ||
        result == (struct exec_elf_load *)0)
        return ENOEXEC;
    bzero(result, sizeof(*result));
    if (!exec_elf_header_valid(header) ||
        header->e_phoff > file_size ||
        (unsigned)header->e_phnum >
        (file_size - header->e_phoff) / sizeof(*phdr))
        return ENOEXEC;

    bzero(&candidate, sizeof(candidate));
    candidate.eel_entry = header->e_entry;
    entry_found = 0;
    for (index = 0; index < header->e_phnum; ++index) {
        phdr = &phdrs[index];
        if (phdr->p_type == PT_NULL)
            continue;
        if (phdr->p_type == PT_INTERP || phdr->p_type == PT_DYNAMIC)
            return ENOEXEC;
        if (phdr->p_type != PT_LOAD)
            continue;
        if (candidate.eel_segment_count >=
            EXEC_ELF_MAX_LOAD_SEGMENTS ||
            phdr->p_memsz == 0 || phdr->p_filesz > phdr->p_memsz ||
            phdr->p_offset > file_size ||
            phdr->p_filesz > file_size - phdr->p_offset ||
            (phdr->p_align > 1 &&
            ((phdr->p_align & (phdr->p_align - 1)) != 0 ||
            ((phdr->p_vaddr - phdr->p_offset) &
            (phdr->p_align - 1)) != 0)) ||
            exec_elf_protection(phdr->p_flags, &protection) != 0 ||
            vm_vaddr_add(phdr->p_vaddr, phdr->p_filesz,
            &file_end) != 0 ||
            vm_vaddr_add(phdr->p_vaddr, phdr->p_memsz,
            &segment_end) != 0 ||
            phdr->p_vaddr < USER_DATA_START ||
            segment_end > USER_DATA_END ||
            vm_vaddr_round_page(segment_end, &map_end) != 0)
            return ENOEXEC;

        segment = &candidate.eel_segments[
            candidate.eel_segment_count];
        segment->eels_map_start = vm_vaddr_trunc_page(phdr->p_vaddr);
        segment->eels_map_size = map_end - segment->eels_map_start;
        segment->eels_vaddr = phdr->p_vaddr;
        segment->eels_file_size = phdr->p_filesz;
        segment->eels_memory_size = phdr->p_memsz;
        segment->eels_file_offset = phdr->p_offset;
        segment->eels_protection = protection;
        if (segment->eels_map_size == 0 ||
            exec_elf_segment_overlap(&candidate,
            segment->eels_map_start,
            map_end))
            return ENOEXEC;
        ++candidate.eel_segment_count;
        if ((protection & VM_PROT_EXECUTE) != 0 &&
            header->e_entry >= phdr->p_vaddr &&
            header->e_entry < file_end)
            entry_found = 1;
    }
    if (candidate.eel_segment_count == 0 || !entry_found)
        return ENOEXEC;

    *result = candidate;
    return 0;
}

int
exec_elf_load_map(struct vmspace *vmspace,
    const struct exec_elf_load *load)
{
    const struct exec_elf_load_segment *segment;
    unsigned index;
    unsigned flags;
    int error;

    if (vmspace == (struct vmspace *)0 ||
        load == (const struct exec_elf_load *)0 ||
        load->eel_segment_count == 0)
        return EINVAL;
    error = 0;
    for (index = 0; index < load->eel_segment_count; ++index) {
        segment = &load->eel_segments[index];
        flags = (segment->eels_protection & VM_PROT_EXECUTE) != 0 ?
            VM_MAP_EXECUTABLE : 0;
        error = vmspace_map_anon(vmspace, segment->eels_map_start,
            segment->eels_map_size, VM_PROT_READ | VM_PROT_WRITE,
            flags);
        if (error != 0)
            break;
    }
    if (error != 0) {
        while (index != 0) {
            --index;
            segment = &load->eel_segments[index];
            (void)vmspace_unmap(vmspace, segment->eels_map_start,
                segment->eels_map_size);
        }
    }
    return error;
}

int
exec_elf_load_finish(struct vmspace *vmspace,
    const struct exec_elf_load *load)
{
    const struct exec_elf_load_segment *segment;
    unsigned index;
    int error;

    if (vmspace == (struct vmspace *)0 ||
        load == (const struct exec_elf_load *)0 ||
        load->eel_segment_count == 0)
        return EINVAL;
    for (index = 0; index < load->eel_segment_count; ++index) {
        segment = &load->eel_segments[index];
        error = vmspace_protect(vmspace, segment->eels_map_start,
            segment->eels_map_size, segment->eels_protection);
        if (error != 0)
            return error;
    }
    return 0;
}
