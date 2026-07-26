#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/exec.h>
#include <sys/exec_aout.h>
#include <sys/dir.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <vm/vmspace.h>

int exec_aout_check(struct exec_params *epp)
{
    vm_size_t image_size;
    vm_vaddr_t bss_start;
    vm_vaddr_t heap_start;
    unsigned stack_size;
    int error;

    if (epp->hdr_len < 0 ||
        (unsigned)epp->hdr_len < sizeof(struct exec))
        return ENOEXEC;
    if (!(N_GETMID(epp->hdr.aout) == MID_ZERO &&
          N_GETFLAG(epp->hdr.aout) == 0))
        return ENOEXEC;

    switch (N_GETMAGIC(epp->hdr.aout)) {
    case OMAGIC:
        if (vm_size_add((vm_size_t)epp->hdr.aout.a_data,
            (vm_size_t)epp->hdr.aout.a_text, &image_size) != 0)
            return ENOEXEC;
        epp->hdr.aout.a_data = image_size;
        epp->hdr.aout.a_text = 0;
        break;
    default:
        printf("Bad a.out magic = %0o\n", N_GETMAGIC(epp->hdr.aout));
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

    DEBUG("Exec file header:\n");
    DEBUG("a_midmag =  %#x\n", epp->hdr.aout.a_midmag);     /* magic number */
    DEBUG("a_text =    %d\n",  epp->hdr.aout.a_text);       /* size of text segment */
    DEBUG("a_data =    %d\n",  epp->hdr.aout.a_data);       /* size of initialized data */
    DEBUG("a_bss =     %d\n",  epp->hdr.aout.a_bss);        /* size of uninitialized data */
    DEBUG("a_reltext = %d\n",  epp->hdr.aout.a_reltext);    /* size of text relocation info */
    DEBUG("a_reldata = %d\n",  epp->hdr.aout.a_reldata);    /* size of data relocation info */
    DEBUG("a_syms =    %d\n",  epp->hdr.aout.a_syms);       /* size of symbol table */
    DEBUG("a_entry =   %#x\n", epp->hdr.aout.a_entry);      /* entry point */

    /*
     * Set up memory allocation
     */
    epp->text.vaddr = epp->heap.vaddr = NO_ADDR;
    epp->text.len = epp->heap.len = 0;

    epp->data.vaddr = (caddr_t)USER_DATA_START;
    epp->data.len = epp->hdr.aout.a_data;
    if (vm_vaddr_add((vm_vaddr_t)USER_DATA_START,
        (vm_size_t)epp->data.len, &bss_start) != 0 ||
        vm_vaddr_add(bss_start, (vm_size_t)epp->hdr.aout.a_bss,
        &heap_start) != 0 || stack_size > (unsigned)USER_DATA_END)
        return ENOEXEC;
    epp->bss.vaddr = (caddr_t)bss_start;
    epp->bss.len = epp->hdr.aout.a_bss;
    epp->heap.vaddr = (caddr_t)heap_start;
    epp->heap.len = 0;
    epp->stack.len = stack_size;
    epp->stack.vaddr = (caddr_t)USER_DATA_END - epp->stack.len;

    /*
     * Allocate core at this point, committed to the new image.
     */
    error = exec_estab(epp);
    if (error) {
        DEBUG("exec_estab returned error=%d\n", error);
        return error;
    }

    /* read in text and data */
    DEBUG("reading a.out image\n");
    error = vmspace_read_inode(epp->vmspace, epp->ip,
        (vm_vaddr_t)epp->data.vaddr, epp->hdr.aout.a_data,
        sizeof(struct exec) + epp->hdr.aout.a_text);
    if (error) {
        DEBUG("read image returned error=%d\n", error);
        return error;
    }

    if ((epp->bss.len != 0 && vmspace_zero(epp->vmspace,
        (vm_vaddr_t)epp->bss.vaddr, epp->bss.len) != 0) ||
        vmspace_zero(epp->vmspace, (vm_vaddr_t)epp->stack.vaddr,
        epp->stack.len) != 0)
        return EFAULT;
    error = exec_setupstack(epp->hdr.aout.a_entry, epp);
    if (error != 0)
        return error;
    error = exec_commit(epp);
    if (error != 0)
        return error;
    exec_clear(epp);

    return 0;
}
