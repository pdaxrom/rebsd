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
exec_elf_segment_overlap(const struct exec_elf_image *image,
    vm_vaddr_t start, vm_vaddr_t end)
{
    const struct exec_elf_segment *segment;
    unsigned index;
    vm_vaddr_t segment_end;

    for (index = 0; index < image->eei_segment_count; ++index) {
        segment = &image->eei_segments[index];
        segment_end = segment->ees_start + segment->ees_size;
        if (start < segment_end && segment->ees_start < end)
            return 1;
    }
    return 0;
}

static int
exec_elf_read_phdr(const unsigned char *bytes, vm_size_t size,
    const struct elf_ehdr *header, unsigned index, struct elf_phdr *phdr)
{
    vm_size_t offset;

    if (index >= header->e_phnum ||
        vm_size_add((vm_size_t)header->e_phoff,
        (vm_size_t)index * sizeof(*phdr), &offset) != 0)
        return ENOEXEC;
    if (offset > size || sizeof(*phdr) > size - offset)
        return ENOEXEC;
    bcopy(bytes + offset, phdr, sizeof(*phdr));
    return 0;
}

int
exec_elf_image_unload(struct vmspace *vmspace,
    struct exec_elf_image *image)
{
    int error;
    int first_error;

    if (vmspace == (struct vmspace *)0 ||
        image == (struct exec_elf_image *)0)
        return EINVAL;
    first_error = 0;
    while (image->eei_segment_count != 0) {
        --image->eei_segment_count;
        error = vmspace_unmap(vmspace,
            image->eei_segments[image->eei_segment_count].ees_start,
            image->eei_segments[image->eei_segment_count].ees_size);
        if (error != 0 && first_error == 0)
            first_error = error;
    }
    image->eei_entry = 0;
    return first_error;
}

int
exec_elf_image_load(struct vmspace *vmspace, const void *image,
    vm_size_t image_size, struct exec_elf_image *result)
{
    const unsigned char *bytes;
    struct exec_elf_image candidate;
    struct elf_ehdr header;
    struct elf_phdr phdr;
    struct exec_elf_segment *segment;
    vm_vaddr_t file_end;
    vm_vaddr_t segment_end;
    vm_vaddr_t map_end;
    vm_prot_t protection;
    unsigned index;
    unsigned segment_index;
    unsigned flags;
    int entry_found;
    int error;

    if (vmspace == (struct vmspace *)0 || image == (const void *)0 ||
        result == (struct exec_elf_image *)0 ||
        image_size < sizeof(header))
        return ENOEXEC;
    bytes = (const unsigned char *)image;
    bcopy(bytes, &header, sizeof(header));
    if (!exec_elf_header_valid(&header) ||
        header.e_phoff > image_size ||
        (unsigned)header.e_phnum > (image_size - header.e_phoff) /
        sizeof(phdr))
        return ENOEXEC;

    bzero(&candidate, sizeof(candidate));
    candidate.eei_entry = header.e_entry;
    entry_found = 0;
    for (index = 0; index < header.e_phnum; ++index) {
        error = exec_elf_read_phdr(bytes, image_size, &header, index,
            &phdr);
        if (error != 0)
            return error;
        if (phdr.p_type == PT_NULL)
            continue;
        if (phdr.p_type != PT_LOAD ||
            candidate.eei_segment_count >=
            EXEC_ELF_MAX_LOAD_SEGMENTS ||
            phdr.p_memsz == 0 || phdr.p_filesz > phdr.p_memsz ||
            phdr.p_offset > image_size ||
            phdr.p_filesz > image_size - phdr.p_offset ||
            (phdr.p_align > 1 &&
            ((phdr.p_align & (phdr.p_align - 1)) != 0 ||
            ((phdr.p_vaddr - phdr.p_offset) &
            (phdr.p_align - 1)) != 0)) ||
            exec_elf_protection(phdr.p_flags, &protection) != 0 ||
            vm_vaddr_add(phdr.p_vaddr, phdr.p_filesz,
            &file_end) != 0 ||
            vm_vaddr_add(phdr.p_vaddr, phdr.p_memsz,
            &segment_end) != 0 ||
            phdr.p_vaddr < USER_DATA_START ||
            segment_end > USER_DATA_END ||
            vm_vaddr_round_page(segment_end, &map_end) != 0)
            return ENOEXEC;

        segment = &candidate.eei_segments[
            candidate.eei_segment_count];
        segment->ees_start = vm_vaddr_trunc_page(phdr.p_vaddr);
        segment->ees_size = map_end - segment->ees_start;
        segment->ees_protection = protection;
        if (segment->ees_size == 0 ||
            exec_elf_segment_overlap(&candidate, segment->ees_start,
            map_end))
            return ENOEXEC;
        ++candidate.eei_segment_count;
        if ((protection & VM_PROT_EXECUTE) != 0 &&
            header.e_entry >= phdr.p_vaddr &&
            header.e_entry < file_end)
            entry_found = 1;
    }
    if (candidate.eei_segment_count == 0 || !entry_found)
        return ENOEXEC;

    candidate.eei_segment_count = 0;
    segment_index = 0;
    for (index = 0; index < header.e_phnum; ++index) {
        error = exec_elf_read_phdr(bytes, image_size, &header, index,
            &phdr);
        if (error != 0)
            goto failed;
        if (phdr.p_type != PT_LOAD)
            continue;
        segment = &candidate.eei_segments[segment_index];
        flags = (segment->ees_protection & VM_PROT_EXECUTE) != 0 ?
            VM_MAP_EXECUTABLE : 0;
        error = vmspace_map_anon(vmspace, segment->ees_start,
            segment->ees_size, VM_PROT_READ | VM_PROT_WRITE, flags);
        if (error != 0)
            goto failed;
        ++candidate.eei_segment_count;
        file_end = phdr.p_vaddr + phdr.p_filesz;
        if (phdr.p_filesz != 0) {
            error = vmspace_write(vmspace, phdr.p_vaddr,
                bytes + phdr.p_offset, phdr.p_filesz);
            if (error != 0)
                goto failed;
        }
        if (phdr.p_memsz > phdr.p_filesz) {
            error = vmspace_zero(vmspace, file_end,
                phdr.p_memsz - phdr.p_filesz);
            if (error != 0)
                goto failed;
        }
        error = vmspace_protect(vmspace, segment->ees_start,
            segment->ees_size, segment->ees_protection);
        if (error != 0)
            goto failed;
        ++segment_index;
    }
    *result = candidate;
    return 0;

failed:
    (void)exec_elf_image_unload(vmspace, &candidate);
    return error;
}
