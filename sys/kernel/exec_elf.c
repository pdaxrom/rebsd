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
#include <sys/fcntl.h>
#include <sys/signalvar.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vm/vmspace.h>

extern char sigcode[], esigcode[];

#ifndef ELF_TARGET_DATA
#define ELF_TARGET_DATA ELFDATA2LSB
#endif

/* round up and down to page boundaries. */
#define ELF_ROUND(a, b)     (((a) + (b) - 1) & ~((b) - 1))
#define ELF_TRUNC(a, b)     ((a) & ~((b) - 1))

/*
 * elf_check(): Prepare an Elf binary's exec package
 *
 * First, set of the various offsets/lengths in the exec package.
 *
 * Then, mark the text image busy (so it can be demand paged) or error
 * out if this is not possible.  Finally, set up vmcmds for the
 * text, data, bss, and stack segments.
 */
int
exec_elf_check(struct exec_params *epp)
{
    struct elf_phdr *ph;
    vm_vaddr_t file_end;
    vm_vaddr_t load_end;
    vm_vaddr_t segment_end;
    unsigned stack_size;
    int error, i, phsize;

    const char elfident[] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3,
                 ELFCLASS32, ELF_TARGET_DATA, EV_CURRENT, ELFOSABI_SYSV, 0};

    /*
     * Check that this is an ELF file that we can handle,
     * and do some sanity checks on the header
     */
    if (epp->hdr_len < sizeof(struct elf_ehdr))
        return ENOEXEC;
    for (i = 0; i < sizeof elfident; i++)
        if (epp->hdr.elf.e_ident[i] !=  elfident[i])
            return ENOEXEC;
    if (epp->hdr.elf.e_type != ET_EXEC)
        return ENOEXEC;
    if (epp->hdr.elf.e_machine != EM_MIPS || epp->hdr.elf.e_version != EV_CURRENT)
        return ENOEXEC;
    if (epp->hdr.elf.e_phentsize != sizeof(struct elf_phdr) || epp->hdr.elf.e_phoff == 0 || epp->hdr.elf.e_phnum == 0)
        return ENOEXEC;
    if (epp->hdr.elf.e_shnum == 0 || epp->hdr.elf.e_shentsize != sizeof(struct elf_shdr))
        return ENOEXEC;

    /*
     * Read program headers
     */
    phsize = epp->hdr.elf.e_phnum * sizeof(struct elf_phdr);
    ph = exec_alloc(phsize, NBPW, epp);
    if (ph == NULL) {
        printf("can't alloc ph[] sz=%d\n", phsize);
        return ENOEXEC;
    }
    if ((error = rdwri(UIO_READ, epp->ip, (caddr_t)ph, phsize, epp->hdr.elf.e_phoff, IO_UNIT, 0)) != 0)
        return ENOEXEC;

    epp->text.len = epp->data.len = epp->bss.len = epp->stack.len = epp->heap.len = 0;
    epp->text.vaddr = epp->data.vaddr = epp->bss.vaddr = epp->stack.vaddr = epp->heap.vaddr = NO_ADDR;

    if (epp->hdr.elf.e_phnum == 1 && ph[0].p_type == PT_LOAD &&
        ph[0].p_flags == (PF_R|PF_W|PF_X) && ph[0].p_filesz != 0 &&
        ph[0].p_filesz < ph[0].p_memsz &&
        vm_vaddr_add((vm_vaddr_t)ph[0].p_vaddr,
        (vm_size_t)ph[0].p_filesz, &file_end) == 0 &&
        vm_vaddr_add((vm_vaddr_t)ph[0].p_vaddr,
        (vm_size_t)ph[0].p_memsz, &segment_end) == 0) {
        /*
         * In the simple a.out type link, in elf format, there is only
         * one loadable segment that is RWE containing everything
         * Here we fix the memory allocation, and we are done.
         */
        epp->data.vaddr = (caddr_t)ph[0].p_vaddr;
        epp->data.len = ph[0].p_memsz;
        epp->heap.vaddr = (caddr_t)segment_end;
        epp->heap.len = 0;
        epp->stack.len = 0;
        epp->stack.vaddr = NO_ADDR;

        /*
         * We assume .bss is the different between the memory data
         * section size and the file size.
         */
        epp->bss.vaddr = (caddr_t)file_end;
        epp->bss.len = ph[0].p_memsz - ph[0].p_filesz;
        epp->data.len = ph[0].p_filesz;
    } else {
        /*
         * At the current moment we don't handle anything else
         * The rest of the code is implemented as need arise.
         */
        return ENOEXEC;
    }

    /*
     * Save arglist
     */
    error = exec_save_args(epp);
    if (error != 0)
        return error;
    error = exec_stack_size(epp, &stack_size);
    if (error != 0)
        return error;
    if (stack_size > (unsigned)USER_DATA_END)
        return ENOEXEC;
    epp->stack.len = stack_size;
    epp->stack.vaddr = (caddr_t)USER_DATA_END - stack_size;

    /*
     * Establish memory
     */
    if ((error = exec_estab(epp)) != 0)
        return error;

    /*
     * Now load the program sections into memory
     */
    for (i = 0; i < epp->hdr.elf.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        /*
         * Sanity check that the load is to our intended address space.
         */
        if (ph[i].p_filesz == 0 || ph[i].p_filesz >= ph[i].p_memsz ||
            vm_vaddr_add((vm_vaddr_t)ph[i].p_vaddr,
            (vm_size_t)ph[i].p_filesz, &load_end) != 0 ||
            (vm_vaddr_t)ph[i].p_vaddr <
            (vm_vaddr_t)epp->data.vaddr ||
            load_end > (vm_vaddr_t)epp->bss.vaddr) {
            return ENOEXEC;
        }
        error = vmspace_read_inode(epp->vmspace, epp->ip,
            ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_offset);
        if (error != 0)
            return error;
    }

    if ((epp->bss.len != 0 && vmspace_zero(epp->vmspace,
        (vm_vaddr_t)epp->bss.vaddr, epp->bss.len) != 0) ||
        vmspace_zero(epp->vmspace, (vm_vaddr_t)epp->stack.vaddr,
        epp->stack.len) != 0)
        return EFAULT;
    error = exec_setupstack(epp->hdr.elf.e_entry, epp);
    if (error != 0)
        return error;
    error = exec_commit(epp);
    if (error != 0)
        return error;
    exec_clear(epp);

    return 0;
}
