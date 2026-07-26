#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/inode.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/exec.h>
#include <sys/dir.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <vm/vmspace.h>
#ifdef N64
#include <machine/fpu.h>
#endif

#define STACK_ALIGN     (2 * NBPW)
#define STACK_ARG_SLOTS 4

/*
 * How memory is set up.
 *
 * var a:
 * USER_DATA_END: !----------!
 *                ! +P_ssize ! stack
 * p_saddr ->     ¡----------!
 *
 *                !----------!
 *                ! +P_dsize ! .data + .bss + heap
 * P_daddr ->     !----------!
 *
 * var b:
 *
 * USER_DATA_END: !--------!
 *                ! +ssize ! stack
 * saddr ->       ¡--------!
 *                ! +hsize ! heap
 * haddr ->       !--------!
 *                ! +dsize ! .data + .bss
 * daddr ->       !--------!
 *                ! +tsize ! .text
 * taddr ->       !--------!
 * paddr -> +psize
 *
 * var c:
 *                !--------!
 *                ! +tsize ! .text (read only section)
 * taddr ->       !--------!
 *                ! +ssize ! stack
 * saddr ->       ¡--------!
 *                ! +hsize ! heap
 * haddr ->       !--------!
 *                ! +dsize ! .data + .bss
 * daddr ->       !--------!
 * paddr -> +psize
 */

/*
 * Set up user memory.
 *
 * The following is a key to top of the stack and the variables used.
 *
 *  topp->      [argv]      top word for /bin/ps
 *               <0>
 *                n
 *                g
 *                r
 *                a
 *               ...
 *               <0>
 *                0
 *                g
 *                r
 *  ucp ->        a
 *               [0]
 *              [envn]
 *               ...
 *  envp ->     [env0]
 *               [0]
 *              [argn]      ptr to argn
 *               ...
 *  argp ->     [arg0]      ptr to arg0
 *               []
 *               []
 *               []
 *    sp ->      []
 *
 */
int exec_setupstack(unsigned entryaddr, struct exec_params *epp)
{
    int i;
    u_int len, value, stack_pointer;
    vm_size_t arg_pointer_bytes;
    vm_size_t env_pointer_bytes;
    vm_size_t string_bytes;
    vm_vaddr_t argp_address;
    vm_vaddr_t envp_address;
    vm_vaddr_t stack_end;
    vm_vaddr_t topp_address;
    vm_vaddr_t ucp_address;
    char *ucp;
    char **argp, **envp, ***topp;

    DEBUG("exec_setupstack:\n");

    /*
     * Set up top of stack structure as above
     * This depends on that kernel and user spaces
     * map to the same addresses.
     */
    if (epp == 0 || epp->vmspace == 0 ||
        vm_vaddr_add((vm_vaddr_t)epp->stack.vaddr,
        (vm_size_t)epp->stack.len, &stack_end) != 0 ||
        stack_end < NBPW ||
        vm_size_add((vm_size_t)epp->argbc, (vm_size_t)epp->envbc,
        &string_bytes) != 0 || string_bytes > VM_SIZE_MAX - (NBPW - 1))
        return E2BIG;
    string_bytes = (string_bytes + NBPW - 1) &
        ~(vm_size_t)(NBPW - 1);
    if ((vm_size_t)epp->envc + 1 > VM_SIZE_MAX / NBPW ||
        (vm_size_t)epp->argc + 1 > VM_SIZE_MAX / NBPW)
        return E2BIG;
    env_pointer_bytes = ((vm_size_t)epp->envc + 1) * NBPW;
    arg_pointer_bytes = ((vm_size_t)epp->argc + 1) * NBPW;
    topp_address = stack_end - NBPW;
    if (string_bytes > topp_address ||
        env_pointer_bytes > topp_address - string_bytes)
        return E2BIG;
    ucp_address = topp_address - string_bytes;
    envp_address = ucp_address - env_pointer_bytes;
    if (arg_pointer_bytes > envp_address)
        return E2BIG;
    argp_address = envp_address - arg_pointer_bytes;
    if (STACK_ARG_SLOTS * NBPW > argp_address)
        return E2BIG;
    stack_pointer = (argp_address - STACK_ARG_SLOTS * NBPW) &
        ~(STACK_ALIGN - 1);
    if (stack_pointer < (vm_vaddr_t)epp->stack.vaddr)
        return E2BIG;
    topp = (char ***)(unsigned)topp_address;
    ucp = (char *)(unsigned)ucp_address;
    envp = (char **)(unsigned)envp_address;
    argp = (char **)(unsigned)argp_address;
    value = (u_int)argp;
    if (vmspace_write(epp->vmspace, (vm_vaddr_t)topp,
        &value, sizeof(value)) != 0)
        return EFAULT;

    /*
     * copy the arguments into the structure
     */
    //nc = 0;
    for (i = 0; i < epp->argc; i++) {
        value = (u_int)ucp;
        len = strlen(epp->argp[i]) + 1;
        if (vmspace_write(epp->vmspace,
            (vm_vaddr_t)&argp[i], &value, sizeof(value)) != 0 ||
            vmspace_write(epp->vmspace, (vm_vaddr_t)ucp,
            epp->argp[i], len) != 0)
            return EFAULT;
        ucp += len;
    }
    value = 0;
    if (vmspace_write(epp->vmspace, (vm_vaddr_t)&argp[epp->argc],
        &value, sizeof(value)) != 0)
        return EFAULT;

    for (i = 0; i < epp->envc; i++) {
        value = (u_int)ucp;
        len = strlen(epp->envp[i]) + 1;
        if (vmspace_write(epp->vmspace,
            (vm_vaddr_t)&envp[i], &value, sizeof(value)) != 0 ||
            vmspace_write(epp->vmspace, (vm_vaddr_t)ucp,
            epp->envp[i], len) != 0)
            return EFAULT;
        ucp += len;
    }
    value = 0;
    if (vmspace_write(epp->vmspace, (vm_vaddr_t)&envp[epp->envc],
        &value, sizeof(value)) != 0)
        return EFAULT;

    ucp = (caddr_t)roundup((unsigned)ucp, NBPW);
    if ((caddr_t)ucp != (caddr_t)topp) {
        DEBUG("Copying of arg list went wrong, ucp=%#x, topp=%#x\n", ucp, topp);
        panic("exec check");
    }

    epp->entry = entryaddr;
    epp->stack_pointer = stack_pointer;
    epp->arg_pointer = (unsigned)argp;
    epp->env_pointer = (unsigned)envp;
    DEBUG("Setting up new PC=%#x\n", entryaddr);
    return 0;
}

/*
 * A simple memory allocator used within exec code using file buffers as storage.
 * Will return NULL if allocation is not possible.
 * Total max memory allocatable is MAXALLOCBUF*MAXBSIZE
 * Max size of allocatable chunk is MAXBSIZE
 *
 * All memory allocated with this function will be freed by a call
 * to exec_alloc_freeall()
 */
void *exec_alloc(int size, int ru, struct exec_params *epp)
{
    char *cp;
    int i;

    for (i = 0; i < MAXALLOCBUF; i++)
        if (MAXBSIZE - (ru<=1?epp->alloc[i].fill:roundup(epp->alloc[i].fill,ru)) >= size)
            break;
    if (i == MAXALLOCBUF)
        return NULL;
    if (epp->alloc[i].bp == NULL) {
        if ((epp->alloc[i].bp = geteblk()) == NULL) {
            DEBUG("exec_alloc: no buf\n");
            return NULL;
        }
    }
    if (ru > 1)
        epp->alloc[i].fill = roundup(epp->alloc[i].fill, ru);
    cp = epp->alloc[i].bp->b_addr + epp->alloc[i].fill;
    epp->alloc[i].fill += size;
    bzero (cp, size);
    return cp;
}

/*
 * this will deallocate all memory allocated by exec_alloc
 */
void exec_alloc_freeall(struct exec_params *epp)
{
    int i;
    for (i = 0; i < MAXALLOCBUF; i++) {
        if (epp->alloc[i].bp) {
            brelse(epp->alloc[i].bp);
            epp->alloc[i].bp = NULL;
            epp->alloc[i].fill = 0;
        }
    }
    if (epp->vmspace != 0) {
        (void)vmspace_destroy(epp->vmspace);
        epp->vmspace = 0;
    }
}

int
exec_stack_size(struct exec_params *epp, unsigned *result)
{
    vm_size_t pointer_count;
    vm_size_t pointers;
    vm_size_t strings;
    vm_size_t total;

    if (epp == 0 || result == 0 ||
        vm_size_add((vm_size_t)epp->argbc, (vm_size_t)epp->envbc,
        &strings) != 0 || strings > VM_SIZE_MAX - (NBPW - 1))
        return E2BIG;
    strings = (strings + NBPW - 1) & ~(vm_size_t)(NBPW - 1);
    if (vm_size_add((vm_size_t)epp->argc, (vm_size_t)epp->envc,
        &pointer_count) != 0 ||
        vm_size_add(pointer_count, 4, &pointer_count) != 0 ||
        pointer_count > VM_SIZE_MAX / NBPW)
        return E2BIG;
    pointers = pointer_count * NBPW;
    if (vm_size_add((vm_size_t)SSIZE, strings, &total) != 0 ||
        vm_size_add(total, pointers, &total) != 0)
        return E2BIG;
    *result = (unsigned)total;
    return 0;
}

/*
 * Establish memory for the image based on the
 * values picked up from the executable file and stored
 * in the exec params block.
 */
int exec_estab(struct exec_params *epp)
{
    vm_size_t image_size;
    vm_vaddr_t bss_end, bss_start, data_file_end;
    vm_vaddr_t data_start, data_end, stack_start, stack_end;
    int error;
    DEBUG("text =  %#x..%#x, len=%d\n", epp->text.vaddr, epp->text.vaddr+epp->text.len, epp->text.len);
    DEBUG("data =  %#x..%#x, len=%d\n", epp->data.vaddr, epp->data.vaddr+epp->data.len, epp->data.len);
    DEBUG("bss =   %#x..%#x, len=%d\n", epp->bss.vaddr, epp->bss.vaddr+epp->bss.len, epp->bss.len);
    DEBUG("heap =  %#x..%#x, len=%d\n", epp->heap.vaddr, epp->heap.vaddr+epp->heap.len, epp->heap.len);
    DEBUG("stack = %#x..%#x, len=%d\n", epp->stack.vaddr, epp->stack.vaddr+epp->stack.len, epp->stack.len);

    /*
     * Right now we can only handle the simple original a.out
     * case, so we double check for that case here.
     */
    if (epp->text.vaddr != NO_ADDR || epp->data.vaddr == NO_ADDR
        || epp->data.vaddr != (caddr_t)USER_DATA_START || epp->stack.vaddr != (caddr_t)USER_DATA_END - epp->stack.len)
        return ENOMEM;

    /*
     * Try out for overflow
     */
    if (vm_size_add((vm_size_t)epp->text.len,
        (vm_size_t)epp->data.len, &image_size) != 0 ||
        vm_size_add(image_size, (vm_size_t)epp->bss.len,
        &image_size) != 0 ||
        vm_size_add(image_size, (vm_size_t)epp->heap.len,
        &image_size) != 0 ||
        vm_size_add(image_size, (vm_size_t)epp->stack.len,
        &image_size) != 0 || image_size > (vm_size_t)MAXMEM)
        return ENOMEM;

    /*
     * Check for bss and data addresses over limit
    */
    if (vm_vaddr_add((vm_vaddr_t)epp->data.vaddr,
        (vm_size_t)epp->data.len, &data_file_end) != 0 ||
        vm_vaddr_add((vm_vaddr_t)epp->bss.vaddr,
        (vm_size_t)epp->bss.len, &bss_end) != 0 ||
        data_file_end > (vm_vaddr_t)USER_DATA_END ||
        bss_end > (vm_vaddr_t)USER_DATA_END)
        return ENOMEM;

    bss_start = (vm_vaddr_t)epp->bss.vaddr;
    if (data_file_end > VM_VADDR_MAX - (NBPW - 1) ||
        bss_start > VM_VADDR_MAX - (NBPW - 1) ||
        ((data_file_end + NBPW - 1) & ~(vm_vaddr_t)(NBPW - 1)) !=
        ((bss_start + NBPW - 1) & ~(vm_vaddr_t)(NBPW - 1))) {
        DEBUG(".bss do not follow .data\n");
        return ENOMEM;
    }

    data_start = vm_vaddr_trunc_page((vm_vaddr_t)epp->data.vaddr);
    error = vm_vaddr_round_page(bss_end, &data_end);
    if (error != 0)
        return error;
    stack_start = vm_vaddr_trunc_page((vm_vaddr_t)epp->stack.vaddr);
    error = vm_vaddr_add((vm_vaddr_t)epp->stack.vaddr,
        (vm_size_t)epp->stack.len, &stack_end);
    if (error == 0)
        error = vm_vaddr_round_page(stack_end, &stack_end);
    if (error != 0 || data_end > stack_start ||
        stack_start - data_end < VM_PAGE_SIZE)
        return ENOMEM;
    error = vmspace_create(&epp->vmspace);
    if (error != 0)
        return error;
    error = vmspace_map_anon(epp->vmspace, data_start,
        data_end - data_start, VM_PROT_ALL, VM_MAP_EXECUTABLE);
    if (error == 0)
        error = vmspace_map_anon(epp->vmspace, stack_start,
            stack_end - stack_start, VM_PROT_READ | VM_PROT_WRITE,
            VM_MAP_STACK);
    if (error != 0) {
        (void)vmspace_destroy(epp->vmspace);
        epp->vmspace = 0;
    }
    return error;
}

int
exec_commit(struct exec_params *epp)
{
    struct vmspace *old;
    struct proc *p;
    int error;
    int s;

    if (epp == 0 || epp->vmspace == 0)
        return EINVAL;
    p = u.u_procp;
    if (p == 0)
        return EINVAL;
    s = splhigh();
    old = p->p_vmspace;
    p->p_vmspace = epp->vmspace;
    error = vmspace_activate(epp->vmspace);
    if (error != 0) {
        p->p_vmspace = old;
        if (old != 0)
            (void)vmspace_activate(old);
        splx(s);
        return error;
    }
    epp->vmspace = 0;
    p->p_dsize = epp->data.len + epp->bss.len;
    p->p_dmin = p->p_dsize;
    p->p_daddr = (size_t)epp->data.vaddr;
    p->p_ssize = epp->stack.len;
    p->p_saddr = (size_t)epp->stack.vaddr;
    u.u_tsize = epp->text.len;
    u.u_dsize = p->p_dsize;
    u.u_ssize = p->p_ssize;
    u.u_prof.pr_scale = 0;
    splx(s);
    if (old != 0 && vmspace_destroy(old) != 0)
        panic("exec old vmspace");
    return 0;
}


/*
 * Save argv[] and envp[]
 */
#define EXEC_ARG_KERNEL 0
#define EXEC_ARG_USER   1

static int
exec_arg_length(char *string, int string_user, int *length)
{
    unsigned char byte;
    int count;

    if (!string_user) {
        *length = strlen(string) + 1;
        return *length <= MAXBSIZE ? 0 : E2BIG;
    }
    for (count = 1; count <= MAXBSIZE; ++count) {
        if (copyin((caddr_t)string, (caddr_t)&byte, 1) != 0)
            return EFAULT;
        ++string;
        if (byte == 0) {
            *length = count;
            return 0;
        }
    }
    return E2BIG;
}

int exec_save_args(struct exec_params *epp)
{
    u_int argc, len;
    caddr_t cp;
    int ap_user, error, i, l;
    char **argp, *ap;

    epp->argc = epp->envc = 0;
    epp->argbc = epp->envbc = 0;

    argc = 0;
    if ((argp = epp->userargp) != NULL) {
        for (;;) {
            if (copyin((caddr_t)&argp[argc], (caddr_t)&ap,
                sizeof(ap)) != 0)
                return EFAULT;
            if (ap == 0)
                break;
            if (++argc > MAXBSIZE / sizeof(char *))
                return E2BIG;
        }
    }
    if (epp->sh.interpreted) {
        argc++;
        if (epp->sh.interparg[0])
            argc++;
    }
    if (argc != 0) {
        if ((epp->argp = (char **)exec_alloc(argc * sizeof(char *), NBPW, epp)) == NULL)
            return ENOMEM;
        for (;;) {
            ap_user = EXEC_ARG_USER;
            /*
             * For a interpreter script, the arg list is changed to
             * #! <interpreter name> <interpreter arg>
             * arg[0] - the interpreter executable name (path)
             * arg[1] - interpreter arg (optional)
             * arg[2 or 1] - script name
             * arg[3 or 2...] - script arg[1...]
             */
            if (argp) {
                if (copyin((caddr_t)argp, (caddr_t)&ap,
                    sizeof(ap)) != 0)
                    return EFAULT;
                ++argp;
            } else
                ap = NULL;

            if (epp->sh.interpreted) {
                if (epp->argc == 0) {
                    ap = epp->sh.interpname;
                    ap_user = EXEC_ARG_KERNEL;
                } else if (epp->argc == 1 && epp->sh.interparg[0]) {
                    ap = epp->sh.interparg;
                    ap_user = EXEC_ARG_KERNEL;
                    --argp;
                } else if ((epp->argc == 1 || (epp->argc == 2 && epp->sh.interparg[0]))) {
                    ap = epp->userfname;
                    --argp;
                }
            }
            if (ap == 0)
                break;
            error = exec_arg_length(ap, ap_user, &l);
            if (error != 0)
                return error;
            if ((cp = exec_alloc(l, 1, epp)) == NULL)
                return ENOMEM;
            if ((ap_user ? copyinstr(ap, cp, l, &len) :
                copykstr(ap, cp, l, &len)) != 0)
                return EFAULT;
            epp->argp[epp->argc++] = cp;
            epp->argbc += len;;
        }
    }
    argc = 0;
    if ((argp = epp->userenvp) != NULL) {
        for (;;) {
            if (copyin((caddr_t)&argp[argc], (caddr_t)&ap,
                sizeof(ap)) != 0)
                return EFAULT;
            if (ap == 0)
                break;
            if (++argc > MAXBSIZE / sizeof(char *))
                return E2BIG;
        }
    }
    epp->envc = 0;
    epp->envbc = 0;
    if (argc != 0) {
        if ((epp->envp = (char **)exec_alloc(argc * sizeof(char *), NBPW, epp)) == NULL)
            return ENOMEM;
        for (;;) {
            if (argp) {
                if (copyin((caddr_t)argp, (caddr_t)&ap,
                    sizeof(ap)) != 0)
                    return EFAULT;
                ++argp;
            } else
                ap = NULL;
            if (ap == 0)
                break;
            error = exec_arg_length(ap, EXEC_ARG_USER, &l);
            if (error != 0)
                return error;
            if ((cp = exec_alloc(l, 1, epp)) == NULL)
                return ENOMEM;
            if (copyinstr(ap, cp, l, &len) != 0)
                return EFAULT;
            epp->envp[epp->envc++] = cp;
            epp->envbc += len;
        }
    }

    for (i = 0; i < epp->argc; i++)
        DEBUG("arg[%d] = \"%s\"\n", i, epp->argp[i]);

    for (i = 0; i < epp->envc; i++)
        DEBUG("env[%d] = \"%s\"\n", i, epp->envp[i]);
    return 0;
}

void exec_clear(struct exec_params *epp)
{
    char *cp;
    int cc;

    /*
     * set SUID/SGID protections, if no tracing
     */
    if ((u.u_procp->p_flag & P_TRACED) == 0) {
        u.u_uid = epp->uid;
        u.u_procp->p_uid = epp->uid;
        u.u_groups[0] = epp->gid;
    } else
        psignal (u.u_procp, SIGTRAP);
    u.u_svuid = u.u_uid;
    u.u_svgid = u.u_groups[0];

    /*
     * Clear registers.
     */
    md_user_frame_exec(u.u_frame, epp->entry, epp->stack_pointer,
        epp->argc, epp->arg_pointer, epp->env_pointer);
#ifdef N64
    bzero (&u.u_fpu, sizeof u.u_fpu);
#endif

    if (epp->argc != 0)
        (void)copykstr(epp->argp[0], u.u_comm, MAXCOMLEN, 0);

    execsigs (u.u_procp);

    /*
     * Clear (close) files
     */
    for (cp = u.u_pofile, cc = 0; cc <= u.u_lastfile; cc++, cp++) {
        if (*cp & UF_EXCLOSE) {
            (void) closef (u.u_ofile [cc]);
            u.u_ofile [cc] = NULL;
            *cp = 0;
        }
    }
    while (u.u_lastfile >= 0 && u.u_ofile [u.u_lastfile] == NULL)
        u.u_lastfile--;
}
