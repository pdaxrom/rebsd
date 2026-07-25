#include <sys/errno.h>
#include <sys/param.h>
#include <sys/exec_elf.h>
#include <sys/systm.h>
#include <machine/layout.h>
#include <vm/vmspace.h>

#include "elf_bootstrap.h"

#define I386_ELF_MAX_PROGRAM_HEADERS 16u

extern const unsigned char _binary_bootstrap_user_elf_start[];
extern const unsigned char _binary_bootstrap_user_elf_end[];

static int
i386_elf_add(unsigned left, unsigned right, unsigned *result)
{
    if (right > ~left)
        return EOVERFLOW;
    *result = left + right;
    return 0;
}

static unsigned
i386_elf_page_trunc(unsigned address)
{
    return address & ~VM_PAGE_MASK;
}

static int
i386_elf_page_round(unsigned address, unsigned *result)
{
    if ((address & VM_PAGE_MASK) == 0) {
        *result = address;
        return 0;
    }
    if (address > ~VM_PAGE_MASK)
        return EOVERFLOW;
    *result = (address + VM_PAGE_MASK) & ~VM_PAGE_MASK;
    return 0;
}

static int
i386_elf_ident_valid(const struct elf_ehdr *header)
{
    return header->e_ident[EI_MAG0] == ELFMAG0 &&
        header->e_ident[EI_MAG1] == ELFMAG1 &&
        header->e_ident[EI_MAG2] == ELFMAG2 &&
        header->e_ident[EI_MAG3] == ELFMAG3 &&
        header->e_ident[EI_CLASS] == ELFCLASS32 &&
        header->e_ident[EI_DATA] == ELFDATA2LSB &&
        header->e_ident[EI_VERSION] == EV_CURRENT &&
        header->e_ident[EI_OSABI] == ELFOSABI_SYSV &&
        header->e_ident[EI_ABIVERSION] == 0;
}

static int
i386_elf_protection(unsigned flags, unsigned *protection)
{
    unsigned value;

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
i386_elf_segment_overlap(const struct i386_elf_image *image,
    unsigned start, unsigned end)
{
    const struct i386_elf_segment *segment;
    unsigned index;
    unsigned segment_end;

    for (index = 0; index < image->iei_segment_count; ++index) {
        segment = &image->iei_segments[index];
        segment_end = segment->ies_start + segment->ies_size;
        if (start < segment_end && segment->ies_start < end)
            return 1;
    }
    return 0;
}

static int
i386_elf_read_phdr(const unsigned char *bytes, unsigned size,
    const struct elf_ehdr *header, unsigned index, struct elf_phdr *phdr)
{
    unsigned offset;

    if (index >= header->e_phnum ||
        index > (~header->e_phoff / sizeof(*phdr)))
        return ENOEXEC;
    offset = header->e_phoff + index * sizeof(*phdr);
    if (offset > size || sizeof(*phdr) > size - offset)
        return ENOEXEC;
    bcopy(bytes + offset, phdr, sizeof(*phdr));
    return 0;
}

int
i386_elf_unload_image(struct vmspace *vmspace,
    struct i386_elf_image *image)
{
    int error;
    int first_error;

    if (vmspace == (struct vmspace *)0 ||
        image == (struct i386_elf_image *)0)
        return EINVAL;
    first_error = 0;
    while (image->iei_segment_count != 0) {
        --image->iei_segment_count;
        error = vmspace_unmap(vmspace,
            image->iei_segments[image->iei_segment_count].ies_start,
            image->iei_segments[image->iei_segment_count].ies_size);
        if (error != 0 && first_error == 0)
            first_error = error;
    }
    image->iei_entry = 0;
    return first_error;
}

int
i386_elf_load_image(struct vmspace *vmspace, const void *image,
    unsigned image_size, struct i386_elf_image *result)
{
    const unsigned char *bytes;
    struct i386_elf_image candidate;
    struct elf_ehdr header;
    struct elf_phdr phdr;
    struct i386_elf_segment *segment;
    unsigned file_end;
    unsigned segment_end;
    unsigned map_end;
    unsigned protection;
    unsigned index;
    unsigned segment_index;
    unsigned flags;
    int entry_found;
    int error;

    if (vmspace == (struct vmspace *)0 || image == (const void *)0 ||
        result == (struct i386_elf_image *)0 ||
        image_size < sizeof(header))
        return ENOEXEC;
    bytes = (const unsigned char *)image;
    bcopy(bytes, &header, sizeof(header));
    if (!i386_elf_ident_valid(&header) ||
        header.e_type != ET_EXEC || header.e_machine != EM_386 ||
        header.e_version != EV_CURRENT || header.e_flags != 0 ||
        header.e_ehsize != sizeof(header) ||
        header.e_phentsize != sizeof(phdr) ||
        header.e_phnum == 0 ||
        header.e_phnum > I386_ELF_MAX_PROGRAM_HEADERS ||
        header.e_phoff > image_size ||
        (unsigned)header.e_phnum > (image_size - header.e_phoff) /
        sizeof(phdr))
        return ENOEXEC;

    bzero(&candidate, sizeof(candidate));
    candidate.iei_entry = header.e_entry;
    entry_found = 0;
    for (index = 0; index < header.e_phnum; ++index) {
        error = i386_elf_read_phdr(bytes, image_size, &header, index,
            &phdr);
        if (error != 0)
            return error;
        if (phdr.p_type == PT_NULL)
            continue;
        if (phdr.p_type != PT_LOAD ||
            candidate.iei_segment_count >=
            I386_ELF_MAX_LOAD_SEGMENTS ||
            phdr.p_memsz == 0 || phdr.p_filesz > phdr.p_memsz ||
            phdr.p_offset > image_size ||
            phdr.p_filesz > image_size - phdr.p_offset ||
            (phdr.p_align > 1 &&
            ((phdr.p_align & (phdr.p_align - 1)) != 0 ||
            ((phdr.p_vaddr - phdr.p_offset) &
            (phdr.p_align - 1)) != 0)) ||
            i386_elf_protection(phdr.p_flags, &protection) != 0 ||
            i386_elf_add(phdr.p_vaddr, phdr.p_filesz, &file_end) != 0 ||
            i386_elf_add(phdr.p_vaddr, phdr.p_memsz,
            &segment_end) != 0 ||
            phdr.p_vaddr < I386_USER_VADDR_START ||
            segment_end > I386_USER_VADDR_END ||
            i386_elf_page_round(segment_end, &map_end) != 0)
            return ENOEXEC;

        segment = &candidate.iei_segments[
            candidate.iei_segment_count];
        segment->ies_start = i386_elf_page_trunc(phdr.p_vaddr);
        segment->ies_size = map_end - segment->ies_start;
        segment->ies_protection = protection;
        if (segment->ies_size == 0 ||
            i386_elf_segment_overlap(&candidate, segment->ies_start,
            map_end))
            return ENOEXEC;
        ++candidate.iei_segment_count;
        if ((protection & VM_PROT_EXECUTE) != 0 &&
            header.e_entry >= phdr.p_vaddr &&
            header.e_entry < file_end)
            entry_found = 1;
    }
    if (candidate.iei_segment_count == 0 || !entry_found)
        return ENOEXEC;

    candidate.iei_segment_count = 0;
    segment_index = 0;
    for (index = 0; index < header.e_phnum; ++index) {
        error = i386_elf_read_phdr(bytes, image_size, &header, index,
            &phdr);
        if (error != 0)
            goto failed;
        if (phdr.p_type != PT_LOAD)
            continue;
        segment = &candidate.iei_segments[segment_index];
        flags = (segment->ies_protection & VM_PROT_EXECUTE) != 0 ?
            VM_MAP_EXECUTABLE : 0;
        error = vmspace_map_anon(vmspace, segment->ies_start,
            segment->ies_size, VM_PROT_READ | VM_PROT_WRITE, flags);
        if (error != 0)
            goto failed;
        ++candidate.iei_segment_count;
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
        error = vmspace_protect(vmspace, segment->ies_start,
            segment->ies_size, segment->ies_protection);
        if (error != 0)
            goto failed;
        ++segment_index;
    }
    *result = candidate;
    return 0;

failed:
    (void)i386_elf_unload_image(vmspace, &candidate);
    return error;
}

int
i386_elf_load_bootstrap_user(struct vmspace *vmspace,
    struct i386_elf_image *result)
{
    unsigned image_size;

    image_size = (unsigned)(_binary_bootstrap_user_elf_end -
        _binary_bootstrap_user_elf_start);
    return i386_elf_load_image(vmspace,
        _binary_bootstrap_user_elf_start, image_size, result);
}
