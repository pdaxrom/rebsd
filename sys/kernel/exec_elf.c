/*-
 * Copyright (c) 1994 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the NetBSD
 *  Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996 Christopher G. Demetriou
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/map.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/namei.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/exec_elf_loader.h>
#include <sys/fcntl.h>
#include <sys/signalvar.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vm/vmspace.h>

#ifndef ELF_TARGET_DATA
#define ELF_TARGET_DATA ELFDATA2LSB
#endif

static int
exec_elf_set_sections(struct exec_params *epp,
    const struct exec_elf_load *load)
{
    const struct exec_elf_load_segment *segment;
    vm_vaddr_t data_end;
    vm_vaddr_t data_start;
    vm_vaddr_t image_end;
    vm_vaddr_t segment_end;
    vm_vaddr_t text_start;
    vm_size_t image_size;
    vm_size_t text_size;
    unsigned index;

    data_start = VM_VADDR_MAX;
    data_end = 0;
    image_end = USER_DATA_START;
    text_start = VM_VADDR_MAX;
    text_size = 0;
    for (index = 0; index < load->eel_segment_count; ++index) {
        segment = &load->eel_segments[index];
        if (vm_vaddr_add(segment->eels_vaddr,
            segment->eels_memory_size, &segment_end) != 0)
            return ENOEXEC;
        if (segment_end > image_end)
            image_end = segment_end;
        if ((segment->eels_protection & VM_PROT_WRITE) != 0) {
            if (segment->eels_vaddr < data_start)
                data_start = segment->eels_vaddr;
            if (segment_end > data_end)
                data_end = segment_end;
        } else {
            if (segment->eels_vaddr < text_start)
                text_start = segment->eels_vaddr;
            if (vm_size_add(text_size, segment->eels_memory_size,
                &text_size) != 0)
                return ENOMEM;
        }
    }
    if (data_start == VM_VADDR_MAX)
        data_start = data_end = image_end;
    if (text_start == VM_VADDR_MAX)
        text_start = image_end;

    epp->text.vaddr = (caddr_t)(unsigned long)text_start;
    epp->text.len = text_size;
    epp->data.vaddr = (caddr_t)(unsigned long)data_start;
    epp->data.len = data_end - data_start;
    epp->bss.vaddr = NO_ADDR;
    epp->bss.len = 0;
    epp->heap.vaddr = (caddr_t)(unsigned long)data_end;
    epp->heap.len = 0;
    if (vm_size_add((vm_size_t)epp->text.len,
        (vm_size_t)epp->data.len, &image_size) != 0 ||
        vm_size_add(image_size, (vm_size_t)epp->stack.len,
        &image_size) != 0 || image_size > (vm_size_t)MAXMEM)
        return ENOMEM;
    return 0;
}

int
exec_elf_check(struct exec_params *epp)
{
    const struct exec_elf_load_segment *segment;
    struct exec_elf_load load;
    struct elf_phdr *phdrs;
    vm_vaddr_t file_end;
    vm_vaddr_t stack_end;
    vm_vaddr_t stack_start;
    unsigned stack_size;
    int error, phsize;
    unsigned i;

    if (epp->hdr_len < 0 ||
        (unsigned)epp->hdr_len < sizeof(struct elf_ehdr) ||
        epp->ip == (struct inode *)0 || epp->ip->i_size <= 0)
        return ENOEXEC;
    if (!exec_elf_header_valid(&epp->hdr.elf))
        return ENOEXEC;

    phsize = epp->hdr.elf.e_phnum * sizeof(struct elf_phdr);
    phdrs = exec_alloc(phsize, NBPW, epp);
    if (phdrs == NULL) {
        printf("can't alloc ph[] sz=%d\n", phsize);
        return ENOEXEC;
    }
    error = rdwri(UIO_READ, epp->ip, (caddr_t)phdrs, phsize,
        epp->hdr.elf.e_phoff, IO_UNIT, 0);
    if (error != 0)
        return ENOEXEC;
    error = exec_elf_load_plan(&epp->hdr.elf, phdrs,
        (vm_size_t)epp->ip->i_size, &load);
    if (error != 0)
        return error;

    error = exec_save_args(epp);
    if (error != 0)
        return error;
    error = exec_stack_size(epp, &stack_size);
    if (error != 0)
        return error;
    if (stack_size > (unsigned)USER_DATA_END)
        return ENOEXEC;
    epp->stack.len = stack_size;
    epp->stack.vaddr = (caddr_t)(unsigned long)
        ((vm_vaddr_t)USER_DATA_END - stack_size);
    error = exec_elf_set_sections(epp, &load);
    if (error != 0)
        return error;
    stack_start = vm_vaddr_trunc_page(
        (vm_vaddr_t)epp->stack.vaddr);
    error = vm_vaddr_add((vm_vaddr_t)epp->stack.vaddr,
        (vm_size_t)epp->stack.len, &stack_end);
    if (error == 0)
        error = vm_vaddr_round_page(stack_end, &stack_end);
    if (error != 0 || stack_end <= stack_start)
        return ENOMEM;

    error = vmspace_create(&epp->vmspace);
    if (error != 0)
        return error;
    error = exec_elf_load_map(epp->vmspace, &load);
    if (error == 0)
        error = vmspace_map_anon(epp->vmspace, stack_start,
            stack_end - stack_start, VM_PROT_READ | VM_PROT_WRITE,
            VM_MAP_STACK);
    if (error != 0)
        return error;

    for (i = 0; i < load.eel_segment_count; ++i) {
        segment = &load.eel_segments[i];
        file_end = segment->eels_vaddr + segment->eels_file_size;
        if (segment->eels_file_size != 0) {
            error = vmspace_read_inode(epp->vmspace, epp->ip,
                segment->eels_vaddr, segment->eels_file_size,
                (off_t)segment->eels_file_offset);
            if (error != 0)
                return error;
        }
        if (segment->eels_memory_size > segment->eels_file_size &&
            vmspace_zero(epp->vmspace, file_end,
            segment->eels_memory_size -
            segment->eels_file_size) != 0)
            return EFAULT;
    }
    error = exec_elf_load_finish(epp->vmspace, &load);
    if (error != 0)
        return error;
    if (vmspace_zero(epp->vmspace, (vm_vaddr_t)epp->stack.vaddr,
        epp->stack.len) != 0)
        return EFAULT;
    error = exec_setupstack(load.eel_entry, epp);
    if (error != 0)
        return error;
    error = exec_commit(epp);
    if (error != 0)
        return error;
    exec_clear(epp);

    return 0;
}
