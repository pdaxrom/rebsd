#include <sys/param.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/cpu.h>

int
md_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
    (void)newlen;
    if (namelen != 1)
        return ENOTDIR;
    switch (name[0]) {
    case CPU_FREQ_KHZ:
        return EOPNOTSUPP;
    case CPU_RAM_BYTES:
        return sysctl_rdlong(oldp, oldlenp, newp, physmem);
    default:
        return EOPNOTSUPP;
    }
}
