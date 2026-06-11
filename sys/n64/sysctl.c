#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <machine/n64.h>

void
ucall(void)
{
    u.u_error = ENOSYS;
}

void
ufetch(void)
{
    u.u_error = ENOSYS;
}

void
ustore(void)
{
    u.u_error = ENOSYS;
}

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
    int value;
    dev_t dev;

    if (namelen != 1)
        return ENOTDIR;

    switch (name[0]) {
    case CPU_CONSDEV:
        dev = makedev(CONS_MAJOR, CONS_MINOR);
        return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
    case CPU_FREQ_KHZ:
        value = N64_CPU_KHZ;
        return sysctl_rdstruct(oldp, oldlenp, newp, &value, sizeof(value));
    case CPU_COUNT_KHZ:
        value = N64_COUNT_KHZ;
        return sysctl_rdstruct(oldp, oldlenp, newp, &value, sizeof(value));
    case CPU_RDRAM_BYTES:
        value = n64_rdram_size();
        return sysctl_rdstruct(oldp, oldlenp, newp, &value, sizeof(value));
    default:
        return EOPNOTSUPP;
    }
}
